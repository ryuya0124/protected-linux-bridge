/* MIT. A fixed-size diagnostic contract, not a crosvm RAM-import implementation. */
#ifndef PLB_PREALLOCATED_RAM_CONTRACT_H
#define PLB_PREALLOCATED_RAM_CONTRACT_H
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>

#define PLB_HANDOFF_BYTES (16UL * 1024 * 1024)
#define PLB_HANDOFF_MAIN_BYTES (15UL * 1024 * 1024)
#define PLB_HANDOFF_REQUIRED_SEALS 7
#define PLB_HANDOFF_WRITE_SEALS (8 | 16)

static inline int plb_handoff_contract(const struct stat *actual, int access_flags, int seals,
                                       uint64_t device, uint64_t inode) {
    return actual && S_ISREG(actual->st_mode) && actual->st_nlink == 0 &&
           actual->st_size == PLB_HANDOFF_BYTES && (uint64_t)actual->st_dev == device &&
           (uint64_t)actual->st_ino == inode && access_flags >= 0 &&
           (access_flags & O_ACCMODE) == O_RDWR && seals >= 0 &&
           (seals & PLB_HANDOFF_REQUIRED_SEALS) == PLB_HANDOFF_REQUIRED_SEALS &&
           !(seals & PLB_HANDOFF_WRITE_SEALS);
}
#endif
