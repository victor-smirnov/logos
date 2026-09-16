struct Inner { v: i64, s: &'static str }
impl Drop for Inner { fn drop(&mut self) {} }
struct S { f: Inner }
impl Drop for S { fn drop(&mut self) {} }
fn move_in_let() {
    let s = S { f: Inner { v: 1i64, s: "foo" } };
    let g = s.f;
    let _ = g.v;
}
fn main() { move_in_let(); }
