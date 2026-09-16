#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
#[derive(Clone, Copy)]
struct S { v: i64 }
enum E { A(S), B }
fn bump(p: &mut S) { p.v = p.v + 7i64; }
fn lmain() -> i32 {
    let e: E = E::A(S { v: 1i64 });
    match e {
        E::A(s) => { bump(&mut s); },
        E::B => {}
    }
    return 0i32;
}
fn main() {}
