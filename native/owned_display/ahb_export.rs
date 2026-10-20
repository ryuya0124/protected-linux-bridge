// MIT. Private Android 17 Cargo binding for real AHardwareBuffer metadata.
// Matches frameworks/native libs/nativewindow/rust AhbInfo: description bytes
// followed by the opaque native-handle ints, with independently duplicated FDs.
// This does not map buffer contents, strip protection, or fabricate an allocation.

use crate::handle::AhbInfo;
use crate::rutabaga_utils::{RutabagaError, RutabagaResult};
use mesa3d_util::{FromRawDescriptor, OwnedDescriptor};
use std::ffi::c_void;

#[repr(C)]
#[derive(Default)]
struct Description {
    width: u32,
    height: u32,
    layers: u32,
    format: u32,
    usage: u64,
    stride: u32,
    reserved0: u32,
    reserved1: u64,
}

const _: () = assert!(std::mem::size_of::<Description>() == 40);

#[link(name = "nativewindow")]
extern "C" {
    fn AHardwareBuffer_release(buffer: *mut c_void);
    fn AHardwareBuffer_describe(buffer: *const c_void, description: *mut Description);
}

struct Reference(*mut c_void);

impl Drop for Reference {
    fn drop(&mut self) {
        // SAFETY: the renderer transferred one acquired reference to export().
        unsafe { AHardwareBuffer_release(self.0) };
    }
}

/// Consumes one exported renderer AHB reference, releasing it after FD duplication.
pub(super) unsafe fn export(buffer: *mut c_void) -> RutabagaResult<AhbInfo> {
    if buffer.is_null() {
        return Err(RutabagaError::InvalidResourceId);
    }
    // SAFETY: this matches nativewindow::HardwareBuffer::from_raw in AOSP:
    // stream_renderer_export_blob transfers one live AHardwareBuffer reference.
    // The guard also releases that reference on every validation failure.
    let _reference = Reference(buffer);
    let mut description = Description::default();
    unsafe { AHardwareBuffer_describe(buffer, &mut description) };
    if description.width == 0
        || description.height == 0
        || description.width > 16384
        || description.height > 16384
        || description.layers != 1
        || description.reserved0 != 0
        || description.reserved1 != 0
        || description.usage & (1 << 14) != 0
    {
        return Err(RutabagaError::InvalidResourceId);
    }
    // This real VNDK symbol is absent from NDK link stubs. Missing support must
    // fail closed rather than replacing it with guessed handle contents.
    let symbol = unsafe {
        libc::dlsym(
            libc::RTLD_DEFAULT,
            c"AHardwareBuffer_getNativeHandle".as_ptr(),
        )
    };
    if symbol.is_null() {
        return Err(RutabagaError::InvalidResourceId);
    }
    let get_handle: unsafe extern "C" fn(*const c_void) -> *const i32 =
        unsafe { std::mem::transmute(symbol) };
    let handle = unsafe { get_handle(buffer) };
    if handle.is_null() {
        return Err(RutabagaError::InvalidResourceId);
    }
    let header = unsafe { std::slice::from_raw_parts(handle, 3) };
    let (fd_count, int_count) = (header[1], header[2]);
    if header[0] != 12 || !(1..=64).contains(&fd_count) || !(0..=4086).contains(&int_count) {
        return Err(RutabagaError::InvalidResourceId);
    }
    let words =
        unsafe { std::slice::from_raw_parts(handle.add(3), (fd_count + int_count) as usize) };
    let mut fds = Vec::with_capacity(fd_count as usize);
    for original in &words[..fd_count as usize] {
        // Only owned duplicates enter AhbInfo; borrowed renderer FDs stay open.
        let duplicate = unsafe { libc::fcntl(*original, libc::F_DUPFD_CLOEXEC, 0) };
        if duplicate < 0 {
            return Err(RutabagaError::InvalidResourceId);
        }
        fds.push(unsafe { OwnedDescriptor::from_raw_descriptor(duplicate) });
    }
    let mut metadata = Vec::with_capacity(40 + int_count as usize * 4);
    metadata.extend_from_slice(unsafe {
        std::slice::from_raw_parts((&description as *const Description).cast::<u8>(), 40)
    });
    for word in &words[fd_count as usize..] {
        metadata.extend_from_slice(&word.to_ne_bytes());
    }
    Ok(AhbInfo { fds, metadata })
}
