/* MIT. Host-only exercise of the exact guest loader's bounded read helper. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef void VOID;
typedef size_t UINTN;
typedef uint8_t UINT8;
typedef uint64_t EFI_STATUS;
#define EFI_SUCCESS 0
#define EFI_DEVICE_ERROR (UINT64_C(1) << 63 | 7)
#define EFI_INVALID_PARAMETER (UINT64_C(1) << 63 | 2)
#define EFI_ABORTED (UINT64_C(1) << 63 | 21)
#define EFI_ERROR(value) ((value) >> 63)
#include "../../tools/native_uefi_probe/BoundedFileRead.h"

struct source {
    unsigned calls;
    unsigned mode;
    size_t position;
};
static EFI_STATUS read_step(VOID *context, UINTN *bytes, VOID *buffer) {
    struct source *source = context;
    source->calls++;
    assert(*bytes <= 1024 * 1024 && *bytes > 0);
    if (source->mode == 1 && *bytes > 997) {
        *bytes = 997;
    } else if (source->mode == 2) {
        *bytes = 0;
    } else if (source->mode == 3) {
        (*bytes)++;
        return EFI_SUCCESS;
    } else if (source->mode == 4 && source->calls == 2) {
        return EFI_ABORTED;
    }
    memset(buffer, 0x5a, *bytes);
    source->position += *bytes;
    return EFI_SUCCESS;
}
int main(void) {
    const size_t total = 2 * 1024 * 1024 + 1536;
    unsigned char *data = malloc(total + 1);
    assert(data);
    for (unsigned mode = 0; mode < 5; mode++) {
        memset(data, 0, total);
        data[total] = 0xa5;
        struct source source = {.mode = mode};
        size_t completed = 999;
        EFI_STATUS status = plb_uefi_read_exact(read_step, &source, data, total, &completed);
        assert(data[total] == 0xa5);
        if (mode < 2) {
            assert(status == EFI_SUCCESS && completed == total && source.position == total);
            assert(source.calls == (mode ? (total + 996) / 997 : 3));
            for (size_t i = 0; i < total; i++) {
                assert(data[i] == 0x5a);
            }
        } else if (mode < 4) {
            assert(status == EFI_DEVICE_ERROR && completed == 0 && source.calls == 1);
        } else {
            assert(status == EFI_ABORTED && completed == 1024 * 1024 && source.calls == 2);
        }
    }
    size_t completed = 3;
    assert(plb_uefi_read_exact(read_step, NULL, NULL, 0, &completed) == EFI_SUCCESS);
    assert(completed == 0);
    assert(plb_uefi_read_exact(read_step, NULL, data, total, NULL) == EFI_INVALID_PARAMETER);
    assert(plb_uefi_read_exact(NULL, NULL, data, total, &completed) == EFI_INVALID_PARAMETER);
    assert(plb_uefi_read_exact(read_step, NULL, NULL, 1, &completed) == EFI_INVALID_PARAMETER);
    free(data);
    return 0;
}
