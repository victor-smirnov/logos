static V: i64 = 5;
fn eqr<'a>(x: &'a i64, y: &'a i64) -> bool { *x == *y }
fn by_param(s: &'static i64) -> i32 {
    let n: i64 = 5;
    if eqr(s, &n) { return 0; }
    1
}
fn main() { let n: i64 = 5; std::process::exit(if by_param(&V) != 0 { 1 } else if !eqr(&V, &n) { 2 } else { 0 }); }
