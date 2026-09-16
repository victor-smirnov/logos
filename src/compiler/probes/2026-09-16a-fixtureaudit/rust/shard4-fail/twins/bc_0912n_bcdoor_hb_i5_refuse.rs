#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: fn-pointer type spelled with an explicit binder (see i2).
struct T { n: i64 }
struct X<'a> { r: &'a T }
struct F { z: i64 }
impl F { fn make(self: &F) -> T { return T { n: 5i64 }; } }
fn mk<'a>(r: &'a T) -> X<'a> { return X { r: r }; }
fn lmain() -> i32 {
    let f: F = F { z: 0i64 };
    let some: for<'a> fn(&'a T) -> X<'a> = mk;
    let g: X = some(&f.make());
    return g.r.n as i32;
}
fn main() {}
