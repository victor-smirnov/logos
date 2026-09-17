fn main() {
    let mut a: [i64; 2] = [1, 2];
    let e: &i64 = &a[1];
    a[0] = *e + 1;
    let k: &i64 = e;
    std::process::exit((*k - 2) as i32);
}
