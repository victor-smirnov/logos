#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn pick<'c>(a: &'c i64, b: &'c i64) -> &'c i64 { return a; }
fn mk<'a, 'b>(x: &'a i64, y: &'b i64) -> &'a i64 {
    return pick(x, y);
}
fn main() {}
