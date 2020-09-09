#include <cstdio>
#include <deko3d.hpp>
#include <nanovg/framework/CMemPool.h>
#include <nanovg_dk.h>
#include <optional>
#include <tesla.hpp>

extern "C" u64 __nx_vi_layer_id;

#define ASSERT_FATAL(x)                \
    if (Result res = x; R_FAILED(res)) \
    fatalThrow(res)

namespace tsl {

    namespace {

        ViDisplay g_viDisplay;
        ViLayer g_viLayer;
        NWindow g_defaultWin;
        Event g_vsyncEvent;

        Result viAddToLayerStack(ViLayer *layer, ViLayerStack stack) {
            const struct {
                u32 stack;
                u64 layerId;
            } in = {stack, layer->layer_id};

            return serviceDispatchIn(viGetSession_IManagerDisplayService(), 6000, in);
        }

        Result hidsysEnableAppletToGetInputShim(bool enable, u64 aruid) {
            const struct {
                u8 permitInput;
                u64 appletResourceUserId;
            } in = { enable != 0, aruid };

            return serviceDispatchIn(hidsysGetServiceSession(), 503, in);
        }

        void requestForeground(bool enabled) {
            u64 applicationAruid = 0, appletAruid = 0;

            for (u64 programId = 0x0100000000001000UL; programId < 0x0100000000001020UL; programId++) {
                pmdmntGetProcessId(&appletAruid, programId);

                if (appletAruid != 0)
                    hidsysEnableAppletToGetInputShim(!enabled, appletAruid);
            }

            pmdmntGetApplicationProcessId(&applicationAruid);
            hidsysEnableAppletToGetInputShim(!enabled, applicationAruid);

            hidsysEnableAppletToGetInput(true);
        }

        class DkTest {
          public:
            static constexpr unsigned NumFramebuffers   = 2;
            static constexpr uint32_t FramebufferWidth  = 448;
            static constexpr uint32_t FramebufferHeight = 720;
            static constexpr unsigned StaticCmdSize     = 0x1000;

            dk::UniqueDevice device;
            dk::UniqueQueue queue;

            std::optional<CMemPool> pool_images;
            std::optional<CMemPool> pool_code;
            std::optional<CMemPool> pool_data;

            dk::UniqueCmdBuf cmdbuf;

            CMemPool::Handle depthBuffer_mem;
            CMemPool::Handle framebuffers_mem[NumFramebuffers];

            dk::Image depthBuffer;
            dk::Image framebuffers[NumFramebuffers];
            DkCmdList framebuffer_cmdlists[NumFramebuffers];
            dk::UniqueSwapchain swapchain;

            DkCmdList render_cmdlist;

            std::optional<nvg::DkRenderer> renderer;
            NVGcontext *vg;

          public:
            DkTest() {
                // Create the deko3d device
                device = dk::DeviceMaker{}
                             .create();

                // Create the main queue
                queue = dk::QueueMaker{device}.setFlags(DkQueueFlags_Graphics).create();

                // Create the memory pools
                pool_images.emplace(device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 4 * 1024 * 1024);
                pool_code.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code, 128 * 1024);
                pool_data.emplace(device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1 * 1024 * 1024);

                // Create the static command buffer and feed it freshly allocated memory
                cmdbuf                  = dk::CmdBufMaker{device}.create();
                CMemPool::Handle cmdmem = pool_data->allocate(StaticCmdSize);
                cmdbuf.addMemory(cmdmem.getMemBlock(), cmdmem.getOffset(), cmdmem.getSize());

                // Create the framebuffer resources
                createFramebufferResources();

                this->renderer.emplace(FramebufferWidth, FramebufferHeight, this->device, this->queue, *this->pool_images, *this->pool_code, *this->pool_data);
                this->vg = nvgCreateDk(&*this->renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);

                PlFontData font;
                int regular = -1;
                int symbols = -1;

                // Standard font
                if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) {
                    regular = nvgCreateFontMem(this->vg, "regular", (unsigned char *)font.address, font.size, false);
                }

                // Extented font
                if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_NintendoExt))) {
                    symbols = nvgCreateFontMem(this->vg, "symbols", (unsigned char *)font.address, font.size, false);
                }

                nvgAddFallbackFontId(this->vg, regular, symbols);
            }

            ~DkTest() {
                // Destroy the framebuffer resources. This should be done first.
                destroyFramebufferResources();

                // Cleanup vg. This needs to be done first as it relies on the renderer.
                nvgDeleteDk(vg);

                // Destroy the renderer
                this->renderer.reset();
            }

            void createFramebufferResources() {
                // Create layout for the depth buffer
                dk::ImageLayout layout_depthbuffer;
                dk::ImageLayoutMaker{device}
                    .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
                    .setFormat(DkImageFormat_S8)
                    .setDimensions(FramebufferWidth, FramebufferHeight)
                    .initialize(layout_depthbuffer);

                // Create the depth buffer
                depthBuffer_mem = pool_images->allocate(layout_depthbuffer.getSize(), layout_depthbuffer.getAlignment());
                depthBuffer.initialize(layout_depthbuffer, depthBuffer_mem.getMemBlock(), depthBuffer_mem.getOffset());

                // Create layout for the framebuffers
                dk::ImageLayout layout_framebuffer;
                dk::ImageLayoutMaker{device}
                    .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
                    .setFormat(DkImageFormat_BGR565_Unorm)
                    .setDimensions(FramebufferWidth, FramebufferHeight)
                    .initialize(layout_framebuffer);

                // Create the framebuffers
                std::array<DkImage const *, NumFramebuffers> fb_array;
                uint64_t fb_size  = layout_framebuffer.getSize();
                uint32_t fb_align = layout_framebuffer.getAlignment();
                for (unsigned i = 0; i < NumFramebuffers; i++) {
                    // Allocate a framebuffer
                    framebuffers_mem[i] = pool_images->allocate(fb_size, fb_align);
                    framebuffers[i].initialize(layout_framebuffer, framebuffers_mem[i].getMemBlock(), framebuffers_mem[i].getOffset());

                    // Generate a command list that binds it
                    dk::ImageView colorTarget{framebuffers[i]}, depthTarget{depthBuffer};
                    cmdbuf.bindRenderTargets(&colorTarget, &depthTarget);
                    framebuffer_cmdlists[i] = cmdbuf.finishList();

                    // Fill in the array for use later by the swapchain creation code
                    fb_array[i] = &framebuffers[i];
                }

                // Create the swapchain using the framebuffers
                swapchain = dk::SwapchainMaker{device, &g_defaultWin, fb_array}.create();

                // Generate the main rendering cmdlist
                recordStaticCommands();
            }

            void destroyFramebufferResources() {
                // Return early if we have nothing to destroy
                if (!swapchain)
                    return;

                // Make sure the queue is idle before destroying anything
                queue.waitIdle();

                // Clear the static cmdbuf, destroying the static cmdlists in the process
                cmdbuf.clear();

                // Destroy the swapchain
                swapchain.destroy();

                // Destroy the framebuffers
                for (unsigned i = 0; i < NumFramebuffers; i++)
                    framebuffers_mem[i].destroy();

                // Destroy the depth buffer
                depthBuffer_mem.destroy();
            }

            void recordStaticCommands() {
                // Initialize state structs with deko3d defaults
                dk::RasterizerState rasterizerState;
                dk::ColorState colorState;
                dk::ColorWriteState colorWriteState;
                dk::BlendState blendState;

                // Configure the viewport and scissor
                cmdbuf.setViewports(0, {{0.0f, 0.0f, FramebufferWidth, FramebufferHeight, 0.0f, 1.0f}});
                cmdbuf.setScissors(0, {{0, 0, FramebufferWidth, FramebufferHeight}});

                // Clear the color and depth buffers
                cmdbuf.clearColor(0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 0.85f);
                cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);

                // Bind required state
                cmdbuf.bindRasterizerState(rasterizerState);
                cmdbuf.bindColorState(colorState);
                cmdbuf.bindColorWriteState(colorWriteState);

                render_cmdlist = cmdbuf.finishList();
            }

            int BeginFrame() {
                // Acquire a framebuffer from the swapchain (and wait for it to be available)
                int slot = queue.acquireImage(swapchain);

                // Run the command list that attaches said framebuffer to the queue
                queue.submitCommands(framebuffer_cmdlists[slot]);

                // Run the main rendering command list
                queue.submitCommands(render_cmdlist);

                nvgBeginFrame(vg, FramebufferWidth, FramebufferHeight, 1.0f);

                return slot;
            }

            void EndFrame(int slot) {
                nvgEndFrame(vg);

                // Now that we are done rendering, present it to the screen
                queue.presentImage(swapchain, slot);
            }
        };

        std::optional<DkTest> g_Dk;

    }

    void Initialize() {
        ASSERT_FATAL(viInitialize(ViServiceType_Manager));
        ASSERT_FATAL(viOpenDefaultDisplay(&g_viDisplay));
        ASSERT_FATAL(viGetDisplayVsyncEvent(&g_viDisplay, &g_vsyncEvent));
        ASSERT_FATAL(viCreateManagedLayer(&g_viDisplay, static_cast<ViLayerFlags>(0), appletGetAppletResourceUserId(), &__nx_vi_layer_id));
        ASSERT_FATAL(viCreateLayer(&g_viDisplay, &g_viLayer));
        ASSERT_FATAL(viSetLayerScalingMode(&g_viLayer, ViScalingMode_Default));

        if (s32 layerZ = 0; R_SUCCEEDED(viGetZOrderCountMax(&g_viDisplay, &layerZ)) && layerZ > 0)
            ASSERT_FATAL(viSetLayerZ(&g_viLayer, layerZ));

        ASSERT_FATAL(viAddToLayerStack(&g_viLayer, ViLayerStack_Screenshot));

        ASSERT_FATAL(viSetLayerSize(&g_viLayer, 672, 1080));
        ASSERT_FATAL(viSetLayerPosition(&g_viLayer, 0, 0));
        ASSERT_FATAL(nwindowCreateFromLayer(&g_defaultWin, &g_viLayer));
        ASSERT_FATAL(nwindowSetDimensions(&g_defaultWin, 448, 720));

        g_Dk.emplace();
        requestForeground(true);
    }

    void Exit() {
        requestForeground(false);
        g_Dk.reset();

        nwindowClose(&g_defaultWin);
        viDestroyManagedLayer(&g_viLayer);
        eventClose(&g_vsyncEvent);
        viCloseDisplay(&g_viDisplay);
        viExit();
    }

    Event *GetVsyncEvent() {
        return &g_vsyncEvent;
    }

    Context::Context() {
        this->impl = g_Dk->vg;
        this->slot = g_Dk->BeginFrame();
    }

    Context::~Context() {
        g_Dk->EndFrame(this->slot);
    }

}
