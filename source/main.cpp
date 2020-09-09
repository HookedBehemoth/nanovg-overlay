#include <cstdlib>
#include <switch.h>
#include <tesla.hpp>

constexpr const SocketInitConfig sockConf = {
    .bsdsockets_version = 1,

    .tcp_tx_buf_size     = 0x800,
    .tcp_rx_buf_size     = 0x800,
    .tcp_tx_buf_max_size = 0x25000,
    .tcp_rx_buf_max_size = 0x25000,

    .udp_tx_buf_size = 0x800,
    .udp_rx_buf_size = 0x800,

    .sb_efficiency = 1,

    .num_bsd_sessions = 0,
    .bsd_service_type = BsdServiceType_Auto,
};

int nxlink;

extern "C" void userAppInit(void) {
    /* Initialize user services here. */
    socketInitialize(&sockConf);
    nxlink = nxlinkStdio();
}

extern "C" void userAppExit(void) {
    /* Cleanup user services here. */
    close(nxlink);
    socketExit();
}

void drawWindow(NVGcontext *vg, const char *title, float x, float y, float w, float h);

int main() {
    /* Initialize tesla. */
    tsl::Initialize();

    while (appletMainLoop()) {
        hidScanInput();

        u64 kDown;
        for (u32 input = 0; input < 10; ++input) {
            kDown |= hidKeysDown(static_cast<HidControllerID>(input));
        }

        if (kDown & (KEY_B | KEY_PLUS))
            break;

        /* Begin frame on construction and end on destruction. */
        /* Use this handle as an NVGcontext. */
        tsl::Context ctx;

        drawWindow(ctx, "Hello from NanoVG and deko3d", 24.0f, 100.0f, 400.0f, 200.0f);
    }

    /* Cleanup tesla. */
    tsl::Exit();

    return EXIT_SUCCESS;
}

void drawWindow(NVGcontext *vg, const char *title, float x, float y, float w, float h) {
    float cornerRadius = 3.0f;
    NVGpaint shadowPaint;
    NVGpaint headerPaint;

    nvgSave(vg);
    //	nvgClearState(vg);

    // Window
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, cornerRadius);
    nvgFillColor(vg, nvgRGBA(0xd0, 30, 34, 192));
    //	nvgFillColor(vg, nvgRGBA(0,0,0,128));
    nvgFill(vg);

    // Drop shadow
    shadowPaint = nvgBoxGradient(vg, x, y + 2, w, h, cornerRadius * 2, 10, nvgRGBA(0, 0, 0, 128), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, x - 10, y - 10, w + 20, h + 30);
    nvgRoundedRect(vg, x, y, w, h, cornerRadius);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadowPaint);
    nvgFill(vg);

    // Header
    headerPaint = nvgLinearGradient(vg, x, y, x, y + 15, nvgRGBA(255, 255, 255, 8), nvgRGBA(0, 0, 0, 16));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1, y + 1, w - 2, 30, cornerRadius - 1);
    nvgFillPaint(vg, headerPaint);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 0.5f, y + 0.5f + 30);
    nvgLineTo(vg, x + 0.5f + w - 1, y + 0.5f + 30);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 32));
    nvgStroke(vg);

    nvgFontSize(vg, 15.0f);
    nvgFontFace(vg, "regular");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    nvgFontBlur(vg, 2);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 128));
    nvgText(vg, x + w / 2, y + 16 + 1, title, NULL);

    nvgFontBlur(vg, 0);
    nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
    nvgText(vg, x + w / 2, y + 16, title, NULL);

    nvgRestore(vg);
}