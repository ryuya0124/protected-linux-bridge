/* MIT. Guest-only UEFI probe; reads one synthetic disk and requests guest shutdown. */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/FdtClient.h>
#include "probe_contract.generated.h"

#define PROBE_DISK_BYTES (2U * 1024 * 1024)
#define PROBE_CHUNK_BYTES 4096U

static void emit(const CHAR8 *phase, UINT64 first, UINT64 second) {
    CHAR8 line[256];
    UINTN length = AsciiSPrint(line, sizeof(line),
                               "PLB_NATIVE_UEFI nonce=" PLB_UEFI_NONCE " phase=%a a=%Lu b=%Lu\n",
                               phase, first, second);
    SerialPortWrite((UINT8 *)line, length);
}

static EFI_STATUS inspect_dma_window(void) {
    FDT_CLIENT_PROTOCOL *fdt = NULL;
    EFI_STATUS status = gBS->LocateProtocol(&gFdtClientProtocolGuid, NULL, (VOID **)&fdt);
    if (EFI_ERROR(status))
        return status;
    INT32 node;
    status = fdt->FindCompatibleNode(fdt, "restricted-dma-pool", &node);
    if (EFI_ERROR(status))
        return status;
    const UINT64 *reg = NULL;
    UINT32 size = 0;
    status = fdt->GetNodeProperty(fdt, node, "reg", (const VOID **)&reg, &size);
    if (EFI_ERROR(status) || size != 16)
        return EFI_COMPROMISED_DATA;
    UINT64 base = SwapBytes64(reg[0]), bytes = SwapBytes64(reg[1]);
    // Only the configured 32 MiB static shared window inside this 512 MiB VM.
    // No dynamic sharing call, guest executable allocation or host policy write.
    if (bytes != 32ULL * 1024 * 1024 || (base & 4095) || base < 0x80000000ULL ||
        base > 0xa0000000ULL - bytes)
        return EFI_COMPROMISED_DATA;
    emit("dma_window", base, bytes);
    return EFI_SUCCESS;
}

static EFI_STATUS read_synthetic_disk(void) {
    EFI_HANDLE *handles = NULL;
    UINTN count = 0;
    EFI_STATUS status =
        gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &count, &handles);
    if (EFI_ERROR(status))
        return status;
    if (count != 1) {
        FreePool(handles);
        return EFI_NOT_FOUND;
    }
    EFI_BLOCK_IO_PROTOCOL *disk = NULL;
    status = gBS->HandleProtocol(handles[0], &gEfiBlockIoProtocolGuid, (VOID **)&disk);
    FreePool(handles);
    if (EFI_ERROR(status))
        return status;
    EFI_BLOCK_IO_MEDIA *media = disk->Media;
    if (!media || !media->MediaPresent || !media->ReadOnly || media->LogicalPartition ||
        media->BlockSize != 512 || media->LastBlock != PROBE_DISK_BYTES / 512 - 1 ||
        media->IoAlign > PROBE_CHUNK_BYTES)
        return EFI_COMPROMISED_DATA;
    UINT8 *buffer = AllocateAlignedPages(1, PROBE_CHUNK_BYTES);
    if (!buffer)
        return EFI_OUT_OF_RESOURCES;
    UINT64 digest = 14695981039346656037ULL;
    for (UINTN offset = 0; offset < PROBE_DISK_BYTES; offset += PROBE_CHUNK_BYTES) {
        status = disk->ReadBlocks(disk, media->MediaId, offset / 512, PROBE_CHUNK_BYTES, buffer);
        if (EFI_ERROR(status))
            break;
        for (UINTN index = 0; index < PROBE_CHUNK_BYTES; index++) {
            UINTN at = offset + index;
            UINT8 expected = (UINT8)(at * 17 + (at >> 9) * 29 + plb_probe_seed[at % 16]);
            if (buffer[index] != expected) {
                emit("data_mismatch", at, buffer[index]);
                status = EFI_COMPROMISED_DATA;
                break;
            }
            digest = (digest ^ buffer[index]) * 1099511628211ULL;
        }
        if (EFI_ERROR(status))
            break;
    }
    FreeAlignedPages(buffer, 1);
    if (!EFI_ERROR(status))
        emit("read_pass", PROBE_DISK_BYTES, digest);
    return status;
}

VOID PlbNativeProbe(VOID) {
    UINT64 current_el;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(current_el));
    emit("entry", current_el >> 2, 0);
    UINT64 frequency, affinity;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(affinity));
    CHAR8 metadata[256];
    UINTN length = AsciiSPrint(
        metadata, sizeof(metadata),
        "PLB_NATIVE_UEFI_CPU nonce=" PLB_UEFI_NONCE " cntfrq=%Lu mpidr=%Lu\n", frequency, affinity);
    SerialPortWrite((UINT8 *)metadata, length);
    EFI_STATUS status = current_el == 4 ? inspect_dma_window() : EFI_UNSUPPORTED;
    if (!EFI_ERROR(status))
        status = read_synthetic_disk();
    emit(EFI_ERROR(status) ? "failed" : "pass", status, 0);
    // This is an instruction inside the guest, never an Android reboot command.
    emit("guest_shutdown", 0, 0);
    gRT->ResetSystem(EfiResetShutdown, status, 0, NULL);
    // Refuse to continue into another boot option if firmware shutdown returns.
    for (;;)
        CpuDeadLoop();
}
