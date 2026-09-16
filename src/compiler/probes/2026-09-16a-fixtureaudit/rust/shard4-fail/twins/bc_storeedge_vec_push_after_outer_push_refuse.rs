#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let x: i64 = 5i64;
    let mut buffer: Vec<&i64> = Vec::new();
    buffer.push(&x);
    {
        let data: i64 = 1i64;
        buffer.push(&data);
    }
    let _ = buffer.len();
    return 0i32;
}
fn main() {}
