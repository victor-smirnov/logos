fn main() {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(0i64);
    let e: &i64 = &v[1];
    v[(*e + 0i64) as usize] = 9i64;
    std::process::exit(v[0] as i32);
}
