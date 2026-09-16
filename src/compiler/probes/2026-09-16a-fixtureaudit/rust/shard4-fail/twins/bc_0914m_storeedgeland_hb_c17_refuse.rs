#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let x: i64 = 5i64;
    let mut buffer: Vec<&i64> = Vec::new();
    buffer.push(&x);
    {
        let d: i64 = 1i64;
        let r: &mut Vec<&i64> = &mut buffer;
        r.push(&d);
    }
    return buffer.len() as i32;
}
fn main() {}
