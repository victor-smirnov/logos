fn main() {
    let mut a: [i64; 2] = [1, 2];
    let p: *mut i64 = &mut a[0] as *mut i64;
    let q: *mut i64 = &mut a[0] as *mut i64;
    if p < q { std::process::exit(1); }
    let _ = a[1];
}
