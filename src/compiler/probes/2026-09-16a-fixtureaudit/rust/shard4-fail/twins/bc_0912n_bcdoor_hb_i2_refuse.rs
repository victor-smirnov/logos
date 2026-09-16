#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: the fn-pointer type is spelled `for<'a> fn(&'a T) -> X<'a>`; Logos's
// `fn(&T) -> X` leaves the struct's lifetime elided, which is E0106 in Rust.
struct T { n: i64 }
struct X<'a> { r: &'a T }
fn temp() -> T { return T { n: 5i64 }; }
fn mk<'a>(r: &'a T) -> X<'a> { return X { r: r }; }
fn lmain() -> i32 {
    let some: for<'a> fn(&'a T) -> X<'a> = mk;
    let g = some(&temp());
    return g.r.n as i32;
}
fn main() {}
