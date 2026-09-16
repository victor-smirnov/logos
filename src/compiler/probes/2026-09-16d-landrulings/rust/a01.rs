fn main() {
    let mut a: [i32; 4] = [1, 2, 3, 4];
    let p: *mut i32 = &mut a[0] as *mut i32;
    std::process::exit(unsafe { *p });
}
