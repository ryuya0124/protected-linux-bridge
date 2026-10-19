// MIT. Optional developer-only LEND registration comparison. This never changes
// GuestMemory mappings, shared SWIOTLB labels, total RAM, or protection flags.

pub(crate) fn chunks(bytes: u64) -> Vec<(u64, u64)> {
    const LIMIT: u64 = 256 * 1024 * 1024;
    let mut offset = 0;
    let mut result = Vec::new();
    while offset < bytes {
        let size = (bytes - offset).min(LIMIT);
        result.push((offset, size));
        offset += size;
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn exact_subdivision_has_no_gaps_overlap_or_lost_partial_tail() {
        for bytes in [0, 4096, 512 * 1024 * 1024, 5856 * 1024 * 1024] {
            let mut end = 0;
            for (offset, size) in chunks(bytes) {
                assert_eq!(offset, end);
                assert!(size > 0 && size <= 256 * 1024 * 1024);
                end += size;
            }
            assert_eq!(end, bytes);
        }
    }
}
