#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a>(v: &mut Vec<&'static i64>) -> &mut [&'a i64] {
    return v;
}
fn main() {}
