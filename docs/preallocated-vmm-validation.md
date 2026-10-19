# Preallocated-RAM VMM validation

Developer investigation, 2026-10-19. This is separate from the released APK.
The ordinary-memfd adoption path has now been built and run in a diskless
protected Linux VM on SM-S948Q / Android 16 / S948QOPS1AZHL. **4 GiB passed
an all-byte write/readback twice in a protected Linux VM.** This uses 4480 MiB
configured RAM; 4096 MiB of user data is fully checked, with space left for the
guest kernel and its page tables. It does not mean all 4480 MiB was verified.

The earlier [installed-VMM review and 16 MiB primitive](preallocated-ram-handoff.md)
remain historical evidence. The installed stock VMM was not patched or replaced;
a different executable in the app's private `kernel-probes` directory is used
only by the developer runner. This is not a production Android/GPU RAM profile.

## What is implemented

The separate build uses official AOSP crosvm revision
[`8a4abfbaea5567de24e49cc43874c820098987d0`](https://android.googlesource.com/platform/external/crosvm/+/8a4abfbaea5567de24e49cc43874c820098987d0/)
and minijail revision `c6fe710936b84753b99e6d0e9ea634109272f307`.
Only `gunyah,plb-preallocated-ram` are enabled in the baseline. Graphics,
networking, qcow2, USB and balloon support are absent from this diagnostic VMM.
Matching host link libraries are captured read-only; they are not redistributed.
The upstream patch retains the [crosvm BSD notice](../third_party/crosvm-BSD-LICENSE.txt);
original Bridge helpers use the project MIT license.

- The producer creates and prefaults an ordinary memfd, unmaps its preparation
  mappings, seals its size and passes that same FD across exec.
- `GuestMemory::create_shm` adopts it explicitly. There is no fresh-allocation
  fallback, syscall interception, CMA substitution or fake successful seal call.
- Adoption checks device/inode identity, anonymous regular-file ownership,
  read/write access, exact summed RAM length and compatible seals. A second
  adoption refuses. Normal shared-object offsets, LEND protection and the
  separate 32 MiB shared SWIOTLB region are retained.
- One owner lock covers bounded anonymous pressure, normal release, immediate
  preallocation and the VM. Preparation without that continuity was unreliable.
- The guest reports allocation placement in relative 256 MiB bands. Its normal
  zone is used before its low DMA32 zone. Preparing file offsets at/above
  2048 MiB first improved supply to those first allocations without changing
  guest addresses or file offsets.
- The all-content variant writes and verifies every 64-bit word with a
  position-dependent pattern. Page-marker probes only verify the marker at
  each 4 KiB page; they must not be described as all-byte tests.

The runner additionally decodes the real host's PAGE_FAULT exit ABI. The observed
failure is `attempt=-12`. The observer does not retry or alter the resume action.
It does not by itself locate the failing Linux/platform/EL2 allocation. A raw
hypervisor map error 10 from older logs must not be silently assigned to every
new `-12` result.

## Measured results

The machine-readable [aggregate record](data/preallocated-vmm-validation-2026-10-19.json)
includes failed attempts as well as passes. Write progress is not successful
readback; the original VMM can even exit zero after a vCPU crash. The new runner
uses `--extended-status`, and the offline auditor requires positive guest
completion and both policy restorers.

| Configured RAM / requested readback | Preparation | Result |
|---|---|---|
| 512 / 256 MiB | Ordinary prefault only | Page-marker readback passed |
| 1024 / 768 MiB | Plain, whole-VMA advice, then chunked advice | Failed during writes |
| 1024 / 768 MiB | Standard io_uring pin/unpin first | No improvement; one incomplete snapshot refused before VM boot |
| 1024 / 768 MiB | Pressure immediately followed by chunked preparation | Page-marker readback passed |
| 2048 / 1536 MiB | Same sequence | Page-marker readback passed |
| 3072 / 2048 MiB | Atomic pressure + normal-zone-first preparation | Page-marker readback passed |
| 4096 / 3072 MiB | Same preparation | Page-marker readback passed |
| 4096 / 3072 MiB | Same preparation, full-content guest | **3,221,225,472 bytes checked, zero mismatches; repeated successfully** |
| 4480 / 4096 MiB | Same preparation | Stopped after 3840 MiB write progress; no full readback |
| 5120 / 4096 MiB | Same preparation | Stopped after 2752 MiB write progress |
| 4480 / 4096 MiB | Per-block long-term pin/unpin added | Stopped after 2240 MiB write progress |
| 5888 / 4096 MiB | Full-content, normal-zone-first | Stopped after 3712 MiB write progress |
| 5888 / 4096 MiB | Owned aligned collapse added | Stopped after 1280 MiB write progress |
| 4480 / 4096 MiB | Registration-only 256 MiB LEND split | Stopped after 3776 MiB write progress |
| 4480 / 4096 MiB | Only 64 KiB advice enabled | Stopped after 2560 MiB write progress |
| 4096 / 3072 MiB | Six-size 16–512 KiB advice after explicit authorization | Full-content 3 GiB readback passed |
| 4480 / 4096 MiB | Same six-size advice, normal-zone-first, no pin/collapse/split | **4,294,967,296 bytes checked, zero mismatches, repeated successfully** |

The 5888 MiB trial plus 256 MiB reserved execution headroom fits the user's
6144 MiB cap. The pin variant reserves an additional 768 MiB and therefore
rejects that configuration. The largest configured size is not the largest
successfully exercised working set.

A separate 16 MiB memfd collapse test obtained five successful 2 MiB collapse
requests out of eight, retained all contents, and observed 2560 THP-flagged pages.
In the large preparation, 512 bounded requests yielded only three successes;
the complete flag/alignment/contiguity scan observed four 2 MiB folios. Collapse
return codes and aggregate THP coverage do not prove that a whole VM uses PMD
folios. Small success did not reproduce at scale. No mapped file holes,
MADV_REMOVE/DONTNEED refault, global compaction or cache-drop write was used.

## Successful 4 GiB path

After explicit approval of the additional 128/256/512 KiB settings, the initial
16 MiB FD experiment observed 4096 THP pages and only 32 compound heads: the
fresh object was supplied as 512 KiB folios. Both restorers passed and all six
values returned to `never`. The three-size 16/32/64 KiB exception alone had not
established this larger-folio supply.

The 4 GiB successes use the **baseline, unsplit** private VMM and the same guest
kernel/initramfs as the 3 GiB controls. The changed preparation enables the six
approved shmem advice sizes only during a bounded window. It uses the atomic
pressure/release/normal-zone-first sequence, without io_uring pins, PMD collapse,
file holes or a new guest kernel. Both runs emitted the exact full-content byte
count, zero errors, guest completion, native/outer exit zero and both successful
policy restoration markers. All 1,146,880 backing pages were present and
THP-flagged in the pre-donation snapshots. This does not imply that every page
was a 512 KiB folio, nor that the snapshot reserves its physical location.

The tested baseline is built from the final checked-in patch. Its VMM SHA256 is
`962b69814df0a60995f5127a2c1ff94730592eb07bf739d1dfa9d6ca0416dc09`;
full-content runner SHA256 is
`7bd20bbc6481126e1fab611f29868e007c85fd57e6019a5afafab860d7c4c38a`;
the separate initramfs SHA256 is
`f2f8653077793ca547d280bffc1cef83e9f1dc96f21a567124d14856ce3682ff`.
These identify the developer artifacts, not a new APK release.

This establishes a measured protected 4 GiB working set under these conditions.
It does not prove a universal RAM limit fix: available folio geometry varies,
and the precise failed Linux/platform/EL2 allocator remains unidentified. The
additional policy changes are not silently enabled in the released app. Android
17 boot, GPU, persistent storage, devices and long-running workloads need their
own validation before this can become an app profile.

The optional `plb-split-ram` comparison subdivides LEND registrations into at
most 256 MiB while retaining original GuestMemory/FDT ranges and shared labels.
Hot-add refuses in that build so extra static labels cannot be reused. Its
512/256 MiB smoke passed; the 4 GiB workload failed. An earlier subdivision of
GuestMemory itself correctly stopped at the existing DTB/payload same-region
assertion; that version was retired instead of bypassing the assertion. The
successful path does not use either form of splitting.

## Safety and limits

All new tests use a clean host boot after the user's separately authorized
normal reboot. No further Android reboot was requested or executed by these
tools. BAD_PAGE/OOPS admission remains fail-closed, and the old spent exception
marker is retained. Expected root O/W taint stayed 4608, SELinux stayed Enforcing,
and all settings used by each trial were restored to `never`. The final check
confirmed all six approved settings were `never`, with the same Android boot,
system_server, main SystemUI and unrelated trusted-VM process identities. The VM parent also watches new kernel reports and stops only its
owned unreaped child on failure or timeout. A passing boundary snapshot is not
a proof that every hypervisor resource has been recovered.

The ordinary policy controller is limited to the three 16/32/64 KiB keys. The
separate, explicitly authorized medium build adds only 128/256/512 KiB; the
[approval and gates](medium-shmem-policy-proposal.md) are recorded separately. It starts an independent restoration guardian before changes and
waits for a validated preparation-ready pipe, with a 30-second preparation
limit. It restores immediately after readiness and observes the rest of the
VM separately. The independent policy deadline stays at 40 seconds. A missing
notification or failed restoration never becomes a successful trial.

Pressure and VM allocation occur sequentially; they are never combined into a
6 GiB pressure allocation plus VM RAM. Before each pressure batch the minimum
MemAvailable floor is 1536 MiB; VM admission additionally requires its full RAM
and that floor, with extra pin headroom when applicable. Only owned processes'
OOM scores are changed. The tools do not signal other apps, Android core
processes or unrelated VMs; Android may reclaim or kill cached apps normally. Existing Debian/Android disks are not attached or opened by the probes.

The exact exhausted Gunyah resource is still unresolved. Large-folio supply and
allocation order materially affect outcomes, but neither host free RAM, THP
percentage, successful io_uring registration nor configured VM capacity alone
is a sufficient success criterion. The diagnostic success does not establish
Android 17, graphics, long-running stability or a safe automatic app profile.

## Reproduce builds without launching a VM

Use dedicated ignored artifact directories for the two shallow source clones
and captured link libraries. Never point these builders at an installed host
executable or system path. The VMM builder checks source revisions, limits Cargo
parallelism, uses a private Cargo cache and retains at least 20 GiB Mac storage
headroom during compilation.

```sh
python3 tools/build_preallocated_vmm.py \
    --source artifacts/research/aosp-crosvm \
    --minijail artifacts/research/aosp-minijail \
    --host-libraries artifacts/research/host-libs \
    --sdk /path/to/Android/sdk --rust-bin /path/to/rust-1.90/bin \
    --llvm-library /path/to/llvm/lib --output artifacts/research/vmm-build
python3 tools/build_memory_probe.py --sdk /path/to/Android/sdk --full-content
python3 tools/build_preallocated_vm_probe.py --sdk /path/to/Android/sdk \
    --vmm artifacts/research/vmm-build/target/aarch64-linux-android/release/crosvm \
    --build-manifest artifacts/research/vmm-build/vmm-build.json --full-content
python3 -m unittest discover -s tests -v
```

The policy comparison binary is built separately with the NDK, `-Wall -Wextra
-Werror` and `-DPLB_PREALLOCATED_VM_POLICY=1`. That opt-in compile definition is
not enabled in the released APK. Building a helper does not authorize applying
host settings. Installation uses fixed app-private paths, old/new hash checks,
root ownership and non-writable mode; no arbitrary command or FD injection UI
was added. To roll back the experiment, stop the tracked owned trial, verify
policy restoration, then remove only hash-verified developer binaries. The
released APK, installed stock VMM and disk profiles retain their previous paths.

The final NDK builds passed `-Wall -Wextra -Werror`, and **171 Python/native test
methods** passed, including native Rust contract/chunk checks, word corruption
and alias checks, budget rejection, ready-pipe validation and log-audit failures.
Synthetic tests are separate from the two successful real-device 4 GiB runs.
