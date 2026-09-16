#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut x: i64 = 5i64;
    let mut buffer: Vec<&i64> = Vec::new();
    let mut i: i64 = 0i64;
    while i < 2i64 {
        buffer.push(&x);
        i = i + 1i64;
    }
    x = 3i64;
    return buffer.len() as i32;
}
fn main() {}
