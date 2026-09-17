fn main() {
    let mut a: [i64; 2] = [1, 2];
    let e: &i64 = &a[1];
    a[0] = *e + 1;
    let w: i64 = *e + 0;
    std::process::exit((w - 2) as i32);
}
