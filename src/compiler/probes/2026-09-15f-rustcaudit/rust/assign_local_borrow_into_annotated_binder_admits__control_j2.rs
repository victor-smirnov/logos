fn foo<'a>(q: &'a i64) -> i64 {
    let v: i64 = 3i64;
    let mut x = q;
    x = &v;
    return *x;
}
fn main() { let q = 1i64; std::process::exit(foo(&q) as i32); }
