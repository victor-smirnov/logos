use std::ops::{Deref, DerefMut};
struct N { v: i64 }
impl N { fn inc(&mut self) { self.v += 1; } }
struct S { n: N }
struct W { inner: S }
impl Deref for W { type Target = S; fn deref(&self) -> &S { &self.inner } }
impl DerefMut for W { fn deref_mut(&mut self) -> &mut S { &mut self.inner } }
fn main() {
    let mut w = W { inner: S { n: N { v: 1 } } };
    w.n.inc();
    println!("{}", w.inner.n.v);
}
