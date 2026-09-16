fn pick<'a>(a: &'a i64, b: &'a i64) -> &'a i64 { if *a > *b { return a; } return b; }
fn main() {
    let f: for<'z> fn(&'z i64, &'z i64) -> &'z i64 = pick;
    let x: i64 = 32i64;
    let y: i64 = 3i64;
    std::process::exit(*f(&x, &y) as i32);
}
