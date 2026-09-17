fn main() {
    let mut a: [i64; 2] = [1, 2];
    let e: &mut i64 = &mut a[1];
    a[0] = 5;
    *e = 7;
    std::process::exit((a[0] - 5) as i32);
}
