#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct H<'a> { r: &'a i64 }
impl<'a> H<'a> {
    fn set(self: &mut H<'a>, r: &'a i64) { self.r = r; }
}
fn lmain() -> i32 {
    let x: i64 = 5i64;
    let y: i64 = 6i64;
    let mut h: H = H { r: &y };
    h.set(&x);
    {
        let d: i64 = 1i64;
        h.set(&d);
    }
    let v: i64 = *h.r;
    return v as i32;
}
fn main() {}
