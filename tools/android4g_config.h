/* MIT. Separate developer profile; never the existing Android/Debian disks. */
#ifndef PLB_ANDROID4G_CONFIG_H
#define PLB_ANDROID4G_CONFIG_H
#ifdef PLB_ANDROID4G_VULKAN_DIAGNOSTIC
#if !defined(PLB_ANDROID4G_INTERACTIVE) || !defined(PLB_ANDROID4G_CUSTOM_GUEST) ||                 \
    !defined(PLB_ANDROID4G_BUFFER_CAPSET) || defined(PLB_ANDROID4G_DISABLE_RENDERER_VULKAN) ||     \
    defined(PLB_ANDROID4G_SURFACE_INTERACTIVE) || defined(PLB_SURFACE_PRODUCT)
#error "Vulkan diagnostic requires its isolated custom-guest runtime and Vulkan caps"
#endif
#define PLB_ANDROID4G_RUNTIME FILES "/android-vulkan4g-runtime"
#define PLB_ANDROID4G_BASE FILES "/android-guests/cf17-vulkan4096"
#define PLB_ANDROID4G_KERNEL PLB_ANDROID4G_RUNTIME "/guest.Image"
#define PLB_ANDROID4G_PROXY PLB_ANDROID4G_RUNTIME "/adopt-vmm"
#define PLB_ANDROID4G_MANAGER PLB_ANDROID4G_RUNTIME "/manager"
#define PLB_ANDROID4G_VMM PLB_ANDROID4G_RUNTIME "/crosvm"
#elif defined(PLB_ANDROID4G_SURFACE_INTERACTIVE)
#if !defined(PLB_ANDROID4G_INTERACTIVE) || !defined(PLB_ANDROID4G_CUSTOM_GUEST)
#error "Surface mode requires the separate guarded interactive custom-guest diagnostic"
#endif
#define PLB_ANDROID4G_RUNTIME FILES "/android-gpu4g-surface-runtime"
#define PLB_ANDROID4G_BASE FILES "/android-guests/cf17-gpu4096-surface"
#define PLB_ANDROID4G_KERNEL PLB_ANDROID4G_RUNTIME "/guest.Image"
#define PLB_ANDROID4G_PROXY PLB_ANDROID4G_RUNTIME "/adopt-vmm"
#define PLB_ANDROID4G_MANAGER PLB_ANDROID4G_RUNTIME "/manager"
#define PLB_ANDROID4G_VMM PLB_ANDROID4G_RUNTIME "/crosvm"
#elif defined(PLB_ANDROID4G_INTERACTIVE)
#define PLB_ANDROID4G_RUNTIME FILES "/android-gpu4g-runtime"
#define PLB_ANDROID4G_BASE FILES "/android-guests/cf17-gpu4096"
#define PLB_ANDROID4G_KERNEL PLB_ANDROID4G_RUNTIME "/guest.Image"
#define PLB_ANDROID4G_PROXY PLB_ANDROID4G_RUNTIME "/adopt-vmm"
#define PLB_ANDROID4G_MANAGER PLB_ANDROID4G_RUNTIME "/manager"
#define PLB_ANDROID4G_VMM PLB_ANDROID4G_RUNTIME "/crosvm"
#elif defined(PLB_ANDROID4G_CUSTOM_GUEST)
#define PLB_ANDROID4G_BASE FILES "/android-guests/cf17-gpu4096-dma-sync"
#define PLB_ANDROID4G_KERNEL FILES "/kernel-probes/android4g-custom-guest.Image"
#define PLB_ANDROID4G_PROXY FILES "/kernel-probes/android4g-custom-adopt-vmm"
#define PLB_ANDROID4G_MANAGER FILES "/kernel-probes/android4g-custom-manager"
#else
#define PLB_ANDROID4G_KERNEL FILES "/kernels/android17-cuttlefish/Image"
#define PLB_ANDROID4G_PROXY FILES "/kernel-probes/android4g-adopt-vmm"
#define PLB_ANDROID4G_MANAGER FILES "/kernel-probes/android4g-manager"
#define PLB_ANDROID4G_BASE FILES "/android-guests/cf17-gpu4096-adopted"
#endif
#ifndef PLB_ANDROID4G_VMM
#define PLB_ANDROID4G_VMM FILES "/kernel-probes/android4g-crosvm"
#endif
#define PLB_ANDROID4G_COMPAT FILES "/kernel-probes/android4g-loader-compat.so"
#define PLB_ANDROID4G_MEMORY_MIB 4096
#endif
