#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S { v: i64 }
impl S { fn get(&mut self) -> &mut i64 { return &mut self.v; } }
fn mk() -> S { return S { v: 1i64 }; }
fn lmain() -> i32 {
    match mk() {
        s => { let r: &mut i64 = s.get(); *r = 8i64; }
    }
    return 0i32;
}
fn main() {}
