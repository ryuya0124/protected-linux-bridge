// MIT. Developer-only adoption at GuestMemory::create_shm in the pinned private
// Android16 VMM. It deliberately has no fresh-allocation fallback. No syscall,
// memory-policy or hypervisor result is interposed or changed by this module.

const MIN_BYTES: u64 = 64 * 1024 * 1024;
// Leave 256 MiB of the explicit 6 GiB developer budget for execution overhead.
const MAX_BYTES: u64 = 5888 * 1024 * 1024;
const REQUIRED_SEALS: i32 = 7;
const WRITE_SEALS: i32 = 8 | 16;
const ALLOWED_SEALS: i32 = REQUIRED_SEALS | 32;

fn valid_contract(
    size: u64,
    expected_size: u64,
    identity_matches: bool,
    anonymous_regular: bool,
    access: i32,
    seals: i32,
) -> bool {
    (MIN_BYTES..=MAX_BYTES).contains(&size)
        && size == expected_size
        && identity_matches
        && anonymous_regular
        && access >= 0
        && access & 3 == 2
        && seals >= 0
        && seals & REQUIRED_SEALS == REQUIRED_SEALS
        && seals & WRITE_SEALS == 0
        && seals & !ALLOWED_SEALS == 0
}

#[cfg(target_os = "android")]
mod android {
    use super::valid_contract;
    use base::{FromRawDescriptor, SafeDescriptor, SharedMemory};
    use std::sync::atomic::{AtomicBool, Ordering};

    static CONSUMED: AtomicBool = AtomicBool::new(false);
    const FD: i32 = 32;

    pub(crate) fn adopt(size: u64) -> base::Result<SharedMemory> {
        // Guest RAM is constructed before device/vCPU threads. This build may
        // adopt exactly one object; a second construction must fail visibly.
        if CONSUMED.swap(true, Ordering::SeqCst) {
            return Err(base::Error::new(libc::EINVAL));
        }
        let identity =
            std::env::var("PLB_RAM_IDENTITY").map_err(|_| base::Error::new(libc::EINVAL))?;
        std::env::remove_var("PLB_RAM_IDENTITY");
        let mut fields = identity.split(':');
        let mut expected = [0_u64; 3];
        for value in &mut expected {
            let text = fields
                .next()
                .ok_or_else(|| base::Error::new(libc::EINVAL))?;
            if text.is_empty() || !text.bytes().all(|byte| byte.is_ascii_digit()) {
                return Err(base::Error::new(libc::EINVAL));
            }
            *value = text.parse().map_err(|_| base::Error::new(libc::EINVAL))?;
        }
        if fields.next().is_some() {
            return Err(base::Error::new(libc::EINVAL));
        }
        // SAFETY: zeroed stat is a valid output buffer for fstat. FD is a fixed
        // inherited descriptor, checked before taking ownership or mapping it.
        let mut metadata: libc::stat = unsafe { std::mem::zeroed() };
        // SAFETY: metadata lives for the complete call and has the correct ABI.
        if unsafe { libc::fstat(FD, &mut metadata) } != 0 {
            return Err(base::Error::last());
        }
        // SAFETY: these fcntl commands take no third argument and only inspect FD.
        let access = unsafe { libc::fcntl(FD, libc::F_GETFL) };
        // SAFETY: as above; this is an observation, never a fake add-seals result.
        let seals = unsafe { libc::fcntl(FD, libc::F_GET_SEALS) };
        let identity_matches =
            metadata.st_dev == expected[0] && metadata.st_ino == expected[1] && size == expected[2];
        let anonymous_regular = metadata.st_mode & libc::S_IFMT == libc::S_IFREG
            && metadata.st_nlink == 0
            && metadata.st_uid == 0;
        if !valid_contract(
            metadata.st_size.try_into().unwrap_or(0),
            size,
            identity_matches,
            anonymous_regular,
            access,
            seals,
        ) {
            return Err(base::Error::new(libc::EINVAL));
        }
        // SAFETY: setting CLOEXEC affects only this process's validated FD. All
        // required seals already exist; adding them again after SEAL would fail.
        if unsafe { libc::fcntl(FD, libc::F_SETFD, libc::FD_CLOEXEC) } != 0 {
            return Err(base::Error::last());
        }
        // SAFETY: the private launcher relinquished FD ownership at exec. No
        // other SafeDescriptor or File owns this fixed descriptor in the VMM.
        let descriptor = unsafe { SafeDescriptor::from_raw_descriptor(FD) };
        let shm = SharedMemory::from_safe_descriptor(descriptor, size)?;
        eprintln!("PLB_RAM_ADOPT bytes={size} identity=1 seals={seals} imported=1");
        Ok(shm)
    }
}

#[cfg(target_os = "android")]
pub(crate) use android::adopt;

#[cfg(test)]
mod tests {
    use super::*;

    const SIZE: u64 = 512 * 1024 * 1024;

    #[test]
    fn accepts_complete_seals_and_optional_noexec_seal() {
        assert!(valid_contract(SIZE, SIZE, true, true, 2, 7));
        assert!(valid_contract(SIZE, SIZE, true, true, 2, 7 | 32));
    }

    #[test]
    fn rejects_size_mismatch_and_out_of_budget() {
        assert!(!valid_contract(SIZE, SIZE - 4096, true, true, 2, 7));
        assert!(!valid_contract(4096, 4096, true, true, 2, 7));
        assert!(!valid_contract(
            MAX_BYTES + 4096,
            MAX_BYTES + 4096,
            true,
            true,
            2,
            7
        ));
    }

    #[test]
    fn rejects_wrong_identity_and_backing_type() {
        assert!(!valid_contract(SIZE, SIZE, false, true, 2, 7));
        assert!(!valid_contract(SIZE, SIZE, true, false, 2, 7));
    }

    #[test]
    fn rejects_read_only_and_failed_access_inspection() {
        assert!(!valid_contract(SIZE, SIZE, true, true, 0, 7));
        assert!(!valid_contract(SIZE, SIZE, true, true, -1, 7));
    }

    #[test]
    fn rejects_incomplete_write_and_unknown_seals() {
        for seals in [0, 1, 6, 7 | 8, 7 | 16, 7 | 64, -1] {
            assert!(!valid_contract(SIZE, SIZE, true, true, 2, seals));
        }
    }
}
