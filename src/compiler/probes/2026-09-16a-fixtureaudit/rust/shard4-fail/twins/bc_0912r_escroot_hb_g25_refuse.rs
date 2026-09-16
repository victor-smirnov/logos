#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn idg<T>(x: T) -> T { return x; }
fn store<'a>(out: &mut &'a i64) {
    let v: i64 = 1i64;
    *out = idg(&v);
}
fn main() {}
