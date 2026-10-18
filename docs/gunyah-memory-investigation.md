# Gunyah memory-provisioning failure

## Status, 2026-10-19

The previously observed heavy-GUI failure is **not fixed**. A bounded, diskless memory workload now reproduces it without GPU, GUI, network, vsock or persistent disks. It also reproduces with the preinstalled Microdroid guest kernel. This narrows the investigation beyond desktop rendering and the custom Linux kernel.

The tests used the explicitly selected Galaxy SM-S948Q, Android 16 / One UI 80500, host kernel 6.12.30 and the installed protected crosvm/Gunyah path. Only one owned VM ran at a time. The host boot ID remained unchanged and SELinux remained Enforcing. The unrelated trusted VM was left alone. No host kernel/module, partition, policy, sysctl, memory-compaction, cache-drop or reboot operation was used. See [the immutable boundary](host-boundary.md).

## Measured results

The guest writes one byte per 4 KiB across an anonymous mapping, reads every written byte back, holds briefly, frees it and requests its own guest poweroff. A pass requires zero readback errors and an explicit completion marker, not merely VMM exit status zero.

| Guest kernel | VM RAM | Requested write/readback | Result |
|---|---:|---:|---|
| Linux 6.18.57-plb1 | 512 MiB | 256 MiB | Completed, zero errors |
| Linux 6.18.57-plb1 | 768 MiB | 512 MiB | Completed, zero errors |
| Linux 6.18.57-plb1 | 1024 MiB | 768 MiB | Failed; last progress marker 448 MiB |
| Same, VMM-local `--hugepages` | 1024 MiB | 768 MiB | Same failure; last marker 448 MiB |
| Preinstalled Microdroid | 1024 MiB | 768 MiB | Same failure; last marker 448 MiB |
| Android 17 Cuttlefish userspace, 2 CPUs | 640 MiB | Android boot | Same map failure at approximately 10 seconds |
| Android 17 Cuttlefish, 6.12.74 / 4 KiB | 512 MiB | 256 MiB | Completed, zero errors; kernel/initramfs only |
| Linux 6.18, private anonymous backing experiment | 512 MiB | 256 MiB | Completed, zero errors |
| Same anonymous backing experiment | 1024 MiB | 768 MiB | Failed; last marker 512 MiB |

These are samples, not a universal 448/512 MiB limit. Last progress is rounded to 64 MiB; it is not the exact failing allocation size. VM boot memory and host backing layout differ. Passing a short memory probe does not establish long-term desktop stability.

On failure the VMM reported `unknown gh exit reason: 3`, a vCPU error and `vcpu crashed`, then still logged success and returned zero. Published Gunyah UAPI names reason 3 `PAGE_FAULT`. Host diagnostics concurrently reported `Failed to map guest address ...: 10`, followed by `Failed to provide folio ...: -12`. Raw logs, physical/guest addresses and device identifiers stay in the ignored private artifacts directory.

## What the source supports

In the pinned [SM8750 reference memory implementation](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/blob/8e66bb68fe6735e93ef5dd59c184cf6341b0098d/drivers/virt/gunyah/vm_mgr_mem.c), this exact map-error message is after the memory-donation call, at `gunyah_hypercall_addrspace_map`. Its [hypercall definitions](https://github.com/OnePlusOSS/android_kernel_oneplus_sm8750/blob/8e66bb68fe6735e93ef5dd59c184cf6341b0098d/include/linux/gunyah.h) map hypervisor `NOMEM` (10) to Linux `-ENOMEM` (-12). This is a related public implementation, **not verified source matching Samsung's installed binary**. The [crosvm Gunyah backend](https://chromium.googlesource.com/crosvm/crosvm/+/refs/heads/main/hypervisor/src/gunyah/mod.rs) explains the VMM side; that moving source is also not proof of the installed build's exact revision.

The evidence places the observed failure at hypervisor guest-address mapping. It does not identify which internal resource was exhausted. Hypervisor mapping metadata, page-table resources, backing-page layout and interactions with other VM allocations remain hypotheses. A direct host `alloc_pages` failure or host RAM fragmentation alone is not established.

Read-only host observations showed roughly 3–4.5 GiB MemAvailable across samples, few free higher-order blocks, little free CMA, anonymous THP set to `always`, and **shmem THP set to `never`**. That makes the effectiveness of crosvm's huge-page hint uncertain: an accepted flag is not proof that a memfd mapping acquired huge folios. No THP/CMA policy was changed.

After reconnecting over USB, the instrumented runner reproduced the failure with and without `--hugepages`; both stopped after the 512 MiB progress marker. The earlier 448 MiB marker therefore is not a fixed limit. A failing VMM sample reached RSS 652300 KiB (about 637 MiB); `AnonHugePages` and `ShmemPmdMapped` were zero. Shared-memory backing named `crosvm_guest` was observed in that owned process's maps, matching the [published memory constructor](https://chromium.googlesource.com/crosvm/crosvm/+/6aed5cc351af1aad6b5ec9a0045ffc73b630e56b/vm_memory/src/guest_memory.rs). A readback/completion pass and the unchanged host boundary were checked independently.

## VM-local anonymous-backing comparison

`memory_backing_probe.c` is a diagnostic interposer, **not an APK feature or a production fix**. It recognizes the observed large, writable `crosvm_guest` mapping and replaces only that mapping in a diskless probe with private anonymous RAM plus `MADV_HUGEPAGE`. The protected VM flag and Gunyah ioctls are untouched. A process/argument guard requires the exact private probe executable, name, no-balloon and protected mode, and refuses options for disks, GPU, network, vsock or other devices. Shared-memory descriptor backends would not be compatible with this replacement.

The original APEX executable refused an app-private preload library at the linker namespace boundary. No linker configuration was changed. The comparison instead ran a root-private, readonly **byte-identical copy** of the installed crosvm with process-local library paths; the Python driver checks the installed/copy SHA256 before each such run. Neither the original executable nor an Android system file was patched. The preload environment is not propagated through later exec calls.

The low-load probe passed. The 1 GiB/768 MiB probe still produced the same vCPU/mapping failure. The replacement was positively logged, anonymous RSS increased, and `AnonHugePages` remained zero. Thus changing backing alone with huge-page advice did **not** solve it. This does not rule out all larger-folio strategies, nor does it justify automatic deployment to normal VMs. No forced collapse/global compaction, huge-page reservation, policy override or protection change was attempted.

## Reproducible diagnostic tools

```sh
python3 tools/build_memory_probe.py --sdk "$ANDROID_SDK_ROOT"
```

This builds `artifacts/memory-probe.cpio`, `artifacts/memory-runner`, the optional diagnostic library and a SHA256 build manifest. It installs nothing and launches no VM. The guest executable refuses non-PID1 execution. The Android runner has fixed executable/kernel/initramfs/log paths, accepts only listed profiles and bounded RAM sizes, and acquires the same ownership lock as the production managers and storage operations.

For an authorized developer with root ADB: stop the owned VM cleanly through Bridge, create/verify its root-private `files/kernel-probes` directory, and install only these two diagnostic outputs there. Do not invoke the guest `/init` as an Android root program. Do not replace guest kernels or launch probes until their pinned identity is verified. Then, using the actual explicitly selected Galaxy serial:

```sh
python3 tools/run_memory_probe.py \
  --serial "$GALAXY_ADB_SERIAL" --kernel linux618 \
  --memory 512 --touch 256 --output artifacts/memory-probes
```

The host script requires the expected model, root adbd, SELinux Enforcing and memory headroom. Each run gets a separate private output directory. The runner limits VM execution to 25 seconds plus two seconds of termination grace, signals only the child it created, and applies a parent-death signal. A busy profile lock refuses the launch; the script never kills a manager or unrelated VM to make room. Keep all diagnostic results private. Never infer success from exit code zero alone.

`--anonymous-backing` is for the explicitly prepared private-copy experiment above, defaults off, and is never used by the app. `--adb` / `--server-port` allow an isolated local ADB server without interrupting another developer's connections. These do not change a device setting.

The 16 KiB Android SDK 37.2 kernel produced no serial output in a separate bounded diskless probe; its compatibility is unresolved. It is not included in the successful memory matrix above. Android 17 Cuttlefish's 4 KiB kernel passed the small diskless workload. [Android display work](android-guest-display.md) tracks that independently. EL0 CPU ID registers are not alone sufficient to identify the 16 KiB boot failure.

## Next discriminating measurements

1. The low-load pass and failing sample now have per-child RSS/huge-page observations and guest CPU feature reports. Future changes must retain those checks, first-fault records and the host boundary before/after.
2. Separate VM RAM size, touched size and allocation order; keep runs bounded and serialized. Avoid repeatedly provoking the same failure without a new hypothesis.
3. Review VM-local backing/allocation strategies against the installed ABI before testing one. Any improvement must retain protected operation and pass readback plus host-boundary checks. Do not change global THP, compact memory, drop caches, use unrelated CMA regions or weaken Gunyah protections.

Until a cause and correction are verified, retain the conservative experimental preset and the existing stock profile. Do not expose a misleading “Gunyah fixed” or “high-load GPU supported” switch.

The parallel Android 17 userspace probe also encountered guest Linux OOM in a 512 MiB VM after SurfaceFlinger/system_server startup. That message comes from the **guest** kernel and is distinct from host Gunyah address-map `NOMEM`. Its HAL/resource work must be tracked separately; it is not evidence that the larger-memory hypervisor failure was fixed.
