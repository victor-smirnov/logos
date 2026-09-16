#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
#[derive(Clone, Copy)]
struct S { v: i64 }
impl S { fn get(&mut self) -> &mut i64 { return &mut self.v; } }
enum E { A(S), B }
fn lmain() -> i32 {
    let e: E = E::A(S { v: 1i64 });
    match e {
        E::A(s) => { let r: &mut i64 = s.get(); *r = 8i64; },
        E::B => {}
    }
    return 0i32;
}
fn main() {}
