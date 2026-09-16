#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut buffer: Vec<&i64> = Vec::new();
    for i in 0i64..2i64 {
        let data: i64 = i;
        buffer.push(&data);
    }
    let _ = buffer.len();
    return 0i32;
}
fn main() {}
