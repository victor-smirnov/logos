fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
    let mut v: Vec<&i64> = Vec::new();
    v.push(x);
    v.push(y);
    return v[0];
}
fn main() {}
