#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: closure return type left to inference; Logos's `-> X` is E0106 in Rust.
struct T { n: i64 }
struct X<'a> { r: &'a T }
fn temp() -> T { return T { n: 5i64 }; }
fn lmain() -> i32 {
    let c = |r: &T| X { r: r };
    let g = c(&temp());
    return g.r.n as i32;
}
fn main() {}
