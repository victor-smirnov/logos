#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
struct W { v: i64 }
impl W {
  fn keep<'a>(self: &Self, t: &'a i64) -> &'static i64 where 'a: 'static { return t; }
}
fn error(w: &W, u: &i64) -> i64 {
  let r = w.keep(u);
  return *r;
}
fn main() { let w = W { v: 1i64 }; let n: i64 = 2i64; error(&w, &n); }
