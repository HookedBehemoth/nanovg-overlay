#include <cstdio>
#include <switch.h>

extern "C" {
u32 __nx_applet_type                   = AppletType_OverlayApplet;
u32 __nx_fs_num_sessions               = 1;
ViLayerFlags __nx_vi_stray_layer_flags = static_cast<ViLayerFlags>(0);
u32 __nx_nv_transfermem_size           = 0x200000;

void __libnx_init_cwd(void);
void __attribute__((weak)) userAppInit(void);
void __attribute__((weak)) userAppExit(void);

void __appInit(void);
void __appExit(void);
}

#define ASSERT_FATAL(x)                \
    if (Result res = x; R_FAILED(res)) \
    fatalThrow(res)

void __appInit() {
    /* Overwritten in env. */
    __nx_applet_type = AppletType_OverlayApplet;

    /* Initialize default services. */
    ASSERT_FATAL(smInitialize());
    ASSERT_FATAL(setsysInitialize());
    if (hosversionGet() == 0) {
        SetSysFirmwareVersion fw;
        if (R_SUCCEEDED(setsysGetFirmwareVersion(&fw)))
            hosversionSet(MAKEHOSVERSION(fw.major, fw.minor, fw.micro));
    }
    ASSERT_FATAL(appletInitialize());
    ASSERT_FATAL(hidInitialize());
    ASSERT_FATAL(fsInitialize());
    ASSERT_FATAL(fsdevMountSdmc());
    __libnx_init_cwd();

    /* Initialize tesla required services. */
    ASSERT_FATAL(plInitialize(PlServiceType_System));
    ASSERT_FATAL(pmdmntInitialize());
    ASSERT_FATAL(hidsysInitialize());

    /* Initialize user services. */
    if (&userAppInit)
        userAppInit();
}

void __appExit() {
    /* Cleanup user services. */
    if (&userAppExit)
        userAppExit();

    /* Cleanup tesla required services. */
    hidsysExit();
    pmdmntExit();
    plExit();

    /* Cleanup default services. */
    fsExit();
    hidExit();
    appletExit();
    setsysExit();
    smExit();
}
