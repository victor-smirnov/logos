fn main() {
    let mut a: [i32; 4] = [5, 2, 3, 4];
    let p: *mut i32 = (&mut a) as *mut [i32; 4] as *mut i32;
    std::process::exit(unsafe { *p });
}
