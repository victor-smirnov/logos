fn foo<'a>(q: &'a i64) -> i64 {
    let v: i64 = 3i64;
    let mut x: &'a i64 = q;
    x = &v;
    return *x;
}
fn main() {}
