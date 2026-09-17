fn main() {
    let lo: *mut u8 = unsafe { libcmalloc(16) };
    let hi: *mut u8 = unsafe { libcmalloc(16) };
    let a = lo as usize; let b = hi as usize;
    let by_addr = a < b; let by_ptr = lo < hi;
    println!("by_addr={} by_ptr={}", by_addr as i32, by_ptr as i32);
    if by_addr != by_ptr { std::process::exit(1); }
}
unsafe extern "C" { #[link_name = "malloc"] fn libcmalloc(n: usize) -> *mut u8; }
