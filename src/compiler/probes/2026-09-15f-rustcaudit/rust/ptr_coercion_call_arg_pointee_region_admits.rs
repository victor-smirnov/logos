fn keep(p: *const &'static i64) -> i64 {
    return 0i64;
}
fn count(s: &[&'static i64]) -> i64 {
    return s.len() as i64;
}
fn f<'a>(x: & &'a i64, v: &mut Vec<&'a i64>) -> i64 {
    return keep(x) + count(v);
}
fn main() {}
