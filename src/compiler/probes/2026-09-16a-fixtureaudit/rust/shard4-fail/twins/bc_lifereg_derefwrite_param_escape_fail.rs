#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a>(x: &'a i64, y: &i64) -> &'a i64 {
  let mut out: &i64 = x;
  { let d: &mut &i64 = &mut out; *d = y; }
  return out;
}
fn main() {}
