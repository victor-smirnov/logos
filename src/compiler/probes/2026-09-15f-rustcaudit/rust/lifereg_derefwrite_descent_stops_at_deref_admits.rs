struct H<'h> { v: &'h i64 }
fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
    let mut h: H = H { v: x };
    { let r: &mut H = &mut h; (*r).v = y; }
    return h.v;
}
fn main() {}
