/* MIT. Fixed protected Android 17 GLES diagnostic, with bounded GPU BAR space.
 * The host display is headless; real gfxstream rendering is tested in-guest.
 */
#ifndef PLB_ANDROID4G_ARGUMENTS_H
#define PLB_ANDROID4G_ARGUMENTS_H
static const char *const android4g_arguments[] = {
    VMM,
    "--extended-status",
    "run",
    "--name",
    "plb-android17-gpu4096",
    "--disable-sandbox",
    "--protected-vm-without-firmware",
    "--swiotlb",
    "32",
    "--mem",
    "size=4096",
    "--cpus",
    "num-cores=2",
    "--serial",
    "type=unix-stream,path=" PLB_ANDROID4G_BASE
    "/early-console.sock,input-unix-stream=true,console=true,earlycon=true",
    "--params",
    "console=ttyS0 earlycon 8250.nr_uarts=1 rdinit=/init panic=0 printk.devkmsg=on "
    "androidboot.hardware=cutf_cvm androidboot.fstab_suffix=cf.f2fs.cts "
    "androidboot.force_normal_boot=1 androidboot.slot_suffix=_a "
    "androidboot.vbmeta.size=22912 androidboot.vbmeta.hash_alg=sha256 "
    "androidboot.vbmeta.digest=0db3571720daf0bda012722eed14cfe195f4d986e8521c3c0ad2c49ee7351633 "
    "androidboot.boot_devices=10000.pci androidboot.hardware.gralloc=minigbm "
    "androidboot.hardware.hwcomposer=ranchu androidboot.hardware.hwcomposer.mode=client "
    "androidboot.hardware.hwcomposer.display_finder_mode=drm "
    "androidboot.hardware.egl=emulation androidboot.hardware.vulkan=pastel "
    "androidboot.hardware.gltransport=virtio-gpu-pipe androidboot.enable_bootanimation=0 "
    "androidboot.hardware.guest_hwui_renderer=skiagl "
    "androidboot.hardware.guest_disable_renderer_preload=true androidboot.console=ttyS0 "
    "androidboot.vendor.apex.com.android.hardware.gatekeeper=com.android.hardware.gatekeeper."
    "nonsecure "
    "androidboot.vendor.apex.com.android.hardware.keymint=com.android.hardware.keymint.rust_"
    "nonsecure "
    "androidboot.vendor.apex.com.android.hardware.graphics.composer=com.android.hardware.graphics."
    "composer.ranchu "
    "androidboot.vendor.apex.com.google.emulated.camera.provider.hal=com.google.emulated.camera."
    "provider.hal "
    "androidboot.cpuvulkan.version=4202496 androidboot.opengles.version=196608 "
    "androidboot.config.low_ram=false androidboot.vsock_lights_port=6900",
    "--vsock",
    "cid=39821",
    "--block",
    "path=" PLB_ANDROID4G_BASE "/android-work.qcow2,ro=false",
    "--block",
    "path=" PLB_ANDROID4G_BASE "/android-misc.raw,ro=false",
    "--block",
    "path=" PLB_ANDROID4G_BASE "/android-frp.raw,ro=false",
    "--gpu",
    "backend=gfxstream,context-types=gfxstream-gles:gfxstream-composer,"
    "egl=true,gles=true,vulkan=false,glx=false,surfaceless=true,width=480,height=800,"
    "pci-bar-size=268435456",
    "--serial",
    "hardware=virtio-console,num=1,type=unix-stream,path=" PLB_ANDROID4G_BASE
    "/sensor-control.sock,input-unix-stream=true",
    "--serial",
    "hardware=virtio-console,num=2,type=unix-stream,path=" PLB_ANDROID4G_BASE
    "/sensor-data.sock,input-unix-stream=true",
    "--serial",
    "hardware=virtio-console,num=3,type=sink",
    "--serial",
    "hardware=virtio-console,num=4,type=unix-stream,path=" PLB_ANDROID4G_BASE
    "/oemlock.sock,input-unix-stream=true",
    "--initrd",
    INITRAMFS,
    GUEST_KERNEL,
    NULL};
#endif
