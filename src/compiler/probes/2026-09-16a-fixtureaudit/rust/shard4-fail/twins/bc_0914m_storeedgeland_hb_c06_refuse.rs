#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn lmain() -> i32 {
    let mut buffer: Vec<&i64> = Vec::new();
    let mut i: i64 = 0i64;
    {
        let r: &mut Vec<&i64> = &mut buffer;
        while i < 2i64 {
            let d: i64 = i;
            r.push(&d);
            i = i + 1i64;
        }
    }
    return buffer.len() as i32;
}
fn main() {}
