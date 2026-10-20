// MIT. App-private entry only; no window pointer, flush bypass or host service lookup.
// Called once by a future qualified root launcher after configuring the display.
#[no_mangle]
pub unsafe extern "C" fn plb_crosvm_owned_display_main(
    argc: i32,
    argv: *const *const std::ffi::c_char,
) -> i32 {
    static CONSUMED: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
    if CONSUMED.swap(true, std::sync::atomic::Ordering::SeqCst)
        || argc < 1
        || argc > 160
        || argv.is_null()
    {
        return 70;
    }
    let mut arguments = Vec::with_capacity(argc as usize);
    for index in 0..argc as usize {
        let pointer = *argv.add(index);
        if pointer.is_null() {
            return 71;
        }
        let argument = std::ffi::CStr::from_ptr(pointer);
        if argument.to_bytes().len() > 4096 {
            return 72;
        }
        match argument.to_str() {
            Ok(value) => arguments.push(value.to_owned()),
            Err(_) => return 73,
        }
    }
    syslog::early_init();
    let result = crosvm_main(arguments);
    match &result {
        Ok(code) => *code as i32,
        Err(_) => {
            // Detailed upstream diagnostics remain in this owned process's private log.
            eprintln!("PLB_OWNED_VMM_FAILED");
            error_to_exit_code(&result)
        }
    }
}
