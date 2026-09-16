fn main() {
    let mut a: [i32; 4] = [6, 2, 3, 4];
    let p: *mut i32 = &mut a;
    std::process::exit(unsafe { *p });
}
