fn vp<'a>(v: &'a Vec<&'a i64>) -> *const Vec<&'static i64> {
    return v;
}
fn bp<'a>(b: &'a Box<&'a i64>) -> *const Box<&'static i64> {
    return b;
}
fn main() {}
