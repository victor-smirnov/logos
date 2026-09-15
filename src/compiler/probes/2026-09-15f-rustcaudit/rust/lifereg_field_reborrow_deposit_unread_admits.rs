struct W<T> { v: T }
fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
    let mut w: W<&i64> = W { v: x };
    { let r: &mut W<&i64> = &mut w; let p: &mut &i64 = &mut r.v; *p = y; }
    return w.v;
}
fn main() {}
