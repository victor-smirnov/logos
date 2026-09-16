#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S { k: i64 }
impl S {
    fn getk(&self) -> &i64 { return &self.k; }
}
fn foo<'a>(out: &mut &'a i64) {
    let s: S = S { k: 5i64 };
    *out = s.getk();
}
fn main() {}
