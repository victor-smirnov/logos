struct H<'h> { v: &'h i64 }
fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
    let mut out: [H; 1] = [H { v: x }];
    { let s: &mut [H] = &mut out; s[0].v = y; }
    return out[0].v;
}
fn main() {}
