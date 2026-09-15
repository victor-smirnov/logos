static V: i64 = 5i64;
fn eqr<'a>(x: &'a i64, y: &'a i64) -> bool { return *x == *y; }
fn by_param(s: &'static i64) -> i32 {
    let n: i64 = 5i64;
    if eqr(s, &n) { return 0i32; }
    return 1i32;
}
fn logos_main() -> i32 {
    let n: i64 = 5i64;
    if by_param(&V) != 0i32 { return 1i32; }
    if !eqr(&V, &n) { return 2i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
