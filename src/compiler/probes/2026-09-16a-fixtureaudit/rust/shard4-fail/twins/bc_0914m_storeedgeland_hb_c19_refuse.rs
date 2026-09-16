#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let x: i64 = 3i64;
    let mut buffer: Vec<&i64> = Vec::new();
    let mut i: i64 = 0i64;
    buffer.push(&x);
    while i < 2i64 {
        let data: i64 = i;
        buffer.push(&data);
        i = i + 1i64;
    }
    return buffer.len() as i32;
}
fn main() {}
