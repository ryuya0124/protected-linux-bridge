/* MIT. Diagnostic-only singleton, serialized by the IOStorage adapter's gate.
 * Only the separate 2 MiB synthetic disk is accepted. No original disk API.
 */
#ifndef PLB_XNU_PCI_MEDIA_DEVICE_H
#define PLB_XNU_PCI_MEDIA_DEVICE_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool plb_media_device_open(void);
bool plb_media_device_read(uint64_t aligned_byte_offset, uint8_t destination[4096]);
bool plb_media_device_close(void);
uint64_t plb_media_device_requests(void);
#ifdef __cplusplus
}
#endif
#endif
