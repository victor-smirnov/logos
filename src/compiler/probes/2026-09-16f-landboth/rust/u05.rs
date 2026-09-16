fn val(r: &i64) -> i64 { *r + 1i64 }
fn main() {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let e: &i64 = &v[1];
    v[0] = val(e);
    std::process::exit(v[0] as i32);
}
