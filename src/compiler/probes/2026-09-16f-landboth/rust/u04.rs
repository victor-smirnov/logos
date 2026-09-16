fn main() {
    let mut v: Vec<i64> = Vec::new();
    let mut w: Vec<i64> = Vec::new();
    v.push(1i64); v.push(0i64);
    w.push(0i64); w.push(7i64);
    let e: &i64 = &w[0];
    v[*e as usize] = 9i64;
    std::process::exit(v[0] as i32);
}
