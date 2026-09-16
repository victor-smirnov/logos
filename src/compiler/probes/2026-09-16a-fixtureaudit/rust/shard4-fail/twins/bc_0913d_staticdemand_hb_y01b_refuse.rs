#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
static V: i64 = 5i64;
struct H<'a> { r: &'a i64 }
static mut HS: H<'static> = H { r: &V };
fn set() {
    let n: i64 = 1i64;
    unsafe { HS = H { r: &n }; }
}
fn main() { set(); }
