fn main() {
    let mut a: [i64; 3] = [1, 2, 3];
    let e: &i64 = &a[1];
    let f: &i64 = &a[2];
    a[0] = *e + *f;
    std::process::exit((a[0] - 5) as i32);
}
