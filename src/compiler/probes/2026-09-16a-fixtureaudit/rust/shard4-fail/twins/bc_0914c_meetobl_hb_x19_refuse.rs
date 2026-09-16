#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
static V: i64 = 2i64;
struct P<'a> { x: &'a i64, y: &'a i64 }
fn need_static(p: P<'static>) -> i64 { return *p.x + *p.y; }
fn g<'a>(u: &'a i64) -> i64 {
    return need_static(P { x: &V, y: u });
}
fn main() {}
