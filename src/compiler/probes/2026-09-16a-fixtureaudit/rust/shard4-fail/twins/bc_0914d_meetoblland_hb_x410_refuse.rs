#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S<'a> { p: *const &'a i64 }
fn mk<'a>(p: *const &'a i64) -> S<'static> {
    return S { p: p };
}
fn main() {}
