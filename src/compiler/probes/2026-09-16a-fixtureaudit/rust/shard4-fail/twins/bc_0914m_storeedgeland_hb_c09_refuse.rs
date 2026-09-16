#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut x: i64 = 5i64;
    let y: i64 = 7i64;
    let mut buffer: Vec<&i64> = Vec::new();
    buffer.push(&y);
    buffer.push(&x);
    x = 9i64;
    return buffer.len() as i32;
}
fn main() {}
