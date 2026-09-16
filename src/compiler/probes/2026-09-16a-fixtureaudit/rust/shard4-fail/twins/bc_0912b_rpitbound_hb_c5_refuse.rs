#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S { base: i64 }
impl S {
    fn maker(&self) -> impl Fn() -> i64 {
        let mut n: i64 = self.base;
        return move || -> i64 { n += 1i64; return n; };
    }
}
fn lmain() -> i32 {
    let s: S = S { base: 3i64 };
    let f = s.maker();
    return f() as i32;
}
fn main() {}
