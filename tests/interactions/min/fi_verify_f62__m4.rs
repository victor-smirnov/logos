fn main() {
    let mut x = 5i64;
    let p: *mut i64 = &mut x as *mut _;
    unsafe { *p = 7; }
    println!("{}", x);
}
