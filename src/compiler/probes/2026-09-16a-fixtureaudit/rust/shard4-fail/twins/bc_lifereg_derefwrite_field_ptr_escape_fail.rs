#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: `struct H { r: &mut &i64 }` is E0106 in Rust; the struct takes explicit
// lifetime parameters, which is the only way to spell it.
struct H<'p, 'q> { r: &'p mut &'q i64 }
fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
  let mut out: &i64 = x;
  { let h: H = H { r: &mut out }; *h.r = y; }
  return out;
}
fn main() {}
