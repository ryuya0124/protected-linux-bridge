/* MIT. Guest-only, unattended XNU serial probe. No Apple OS rootfs is attached. */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include "UniXNULoader.h"
#include "probe_contract.generated.h"

static void receipt(const CHAR8 *phase, EFI_STATUS status) {
    CHAR8 line[256];
    UINTN bytes = AsciiSPrint(line, sizeof(line),
                              "PLB_NATIVE_XNU nonce=" PLB_UEFI_NONCE " phase=%a status=%Lu\n",
                              phase, (UINT64)status);
    SerialPortWrite((UINT8 *)line, bytes);
}

static EFI_STATUS boot_xnu(void) {
    UINT64 level;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(level));
    if (level != 4) {
        return EFI_UNSUPPORTED;
    }
    EFI_HANDLE *volumes = NULL;
    UINTN count = 0;
    EFI_STATUS status = gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL,
                                                &count, &volumes);
    if (EFI_ERROR(status)) {
        return status;
    }
    if (count != 1) {
        FreePool(volumes);
        return EFI_NOT_FOUND;
    }
    EFI_HANDLE volume = volumes[0];
    FreePool(volumes);
    EFI_BLOCK_IO_PROTOCOL *block = NULL;
    status = gBS->HandleProtocol(volume, &gEfiBlockIoProtocolGuid, (VOID **)&block);
    if (EFI_ERROR(status) || block == NULL || block->Media == NULL || !block->Media->ReadOnly ||
        !block->Media->MediaPresent || block->Media->BlockSize != 512 ||
        block->Media->LastBlock != 131071) {
        return EFI_COMPROMISED_DATA;
    }
    EFI_DEVICE_PATH_PROTOCOL *path = FileDevicePath(volume, L"\\DARWIN.EFI");
    if (path == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }
    EFI_HANDLE driver = NULL;
    status = gBS->LoadImage(FALSE, gImageHandle, path, NULL, 0, &driver);
    FreePool(path);
    if (EFI_ERROR(status)) {
        return status;
    }
    status = gBS->StartImage(driver, NULL, NULL);
    if (EFI_ERROR(status)) {
        return status;
    }
    EFI_GUID loader_guid = UNIXNU_LOADER_PROTOCOL_GUID;
    UNIXNU_LOADER_PROTOCOL *loader = NULL;
    status = gBS->LocateProtocol(&loader_guid, NULL, (VOID **)&loader);
    if (EFI_ERROR(status) || loader == NULL || loader->Revision != UNIXNU_LOADER_REVISION ||
        loader->Boot == NULL) {
        return EFI_UNSUPPORTED;
    }
    UNIXNU_LOADER_BOOT_REQUEST request;
    ZeroMem(&request, sizeof(request));
    request.Volume = volume;
    request.KernelPath = L"\\XNU";
    request.BootArgs = L"-v debug=0x108 keepsyms=1 -enable_kprintf_spam serial=3 "
                       "disable-uart-irq=1 kt_fixed_zones=120"
#if defined(PLB_XNU_PCI_PROBE) && PLB_XNU_PCI_PROBE
                       " bridge-pci-probe=1"
#endif
        ;
    receipt("loader_ready", EFI_SUCCESS);
    return loader->Boot(loader, &request);
}

/* The pinned firmware boot manager calls this once after connecting devices. */
VOID PlbNativeProbe(VOID) {
    receipt("uefi_entry", EFI_SUCCESS);
    EFI_STATUS status = boot_xnu();
    receipt("loader_returned", status);
    /* This is the guest's PSCI shutdown. No Android reboot is requested. */
    gRT->ResetSystem(EfiResetShutdown, status, 0, NULL);
    for (;;) {
        CpuDeadLoop();
    }
}
