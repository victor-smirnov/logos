#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
use std::ops::{Deref, DerefMut};
struct W { inner: i64 }
impl Deref for W { type Target = i64; fn deref(&self) -> &i64 { return &self.inner; } }
impl DerefMut for W { fn deref_mut(&mut self) -> &mut i64 { return &mut self.inner; } }
fn lmain() -> i32 {
    let w: W = W { inner: 1i64 };
    *w = 7i64;
    return 0i32;
}
fn main() {}
