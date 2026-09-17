fn main() {
    let mut a: [i64; 2] = [1, 2];
    let mut i = 0;
    while i < 2 {
        let e: &i64 = &a[1];
        a[0] = *e + 1;
        i += 1;
    }
    std::process::exit((a[0] - 3) as i32);
}
