/* MIT. Exact EFI file reads without asking one DMA mapping for the whole file.
 * Include after Uefi.h (or equivalent integer/status types in the host test).
 * A short successful read advances only by the bytes actually returned.
 */
#ifndef PLB_BOUNDED_FILE_READ_H
#define PLB_BOUNDED_FILE_READ_H

typedef EFI_STATUS (*PLB_FILE_READ_STEP)(VOID *context, UINTN *bytes, VOID *data);

static EFI_STATUS plb_uefi_read_exact(PLB_FILE_READ_STEP read_step, VOID *context, VOID *data,
                                      UINTN size, UINTN *completed) {
    if (!completed) {
        return EFI_INVALID_PARAMETER;
    }
    *completed = 0;
    if (!read_step || (!data && size)) {
        return EFI_INVALID_PARAMETER;
    }
    while (*completed < size) {
        UINTN requested = size - *completed;
        if (requested > 1024 * 1024) {
            requested = 1024 * 1024;
        }
        UINTN actual = requested;
        EFI_STATUS status = read_step(context, &actual, (UINT8 *)data + *completed);
        if (EFI_ERROR(status)) {
            return status;
        }
        if (!actual || actual > requested) {
            return EFI_DEVICE_ERROR;
        }
        *completed += actual;
    }
    return EFI_SUCCESS;
}
#endif
