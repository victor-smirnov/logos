#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn t<'r>(g: fn(&'r i64) -> i64, h: fn(&'r i64) -> i64) -> i32 {
    let n: i32 = 1i32;
    let f: fn(&i64) -> i64 = match n { 0i32 => g, _ => h };
    return 0i32;
}
fn main() {}
