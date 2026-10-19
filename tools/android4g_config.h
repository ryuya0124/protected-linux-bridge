/* MIT. Separate developer profile; never the existing Android/Debian disks. */
#ifndef PLB_ANDROID4G_CONFIG_H
#define PLB_ANDROID4G_CONFIG_H
#ifdef PLB_ANDROID4G_CUSTOM_GUEST
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
#define PLB_ANDROID4G_VMM FILES "/kernel-probes/android4g-crosvm"
#define PLB_ANDROID4G_COMPAT FILES "/kernel-probes/android4g-loader-compat.so"
#define PLB_ANDROID4G_MEMORY_MIB 4096
#endif
