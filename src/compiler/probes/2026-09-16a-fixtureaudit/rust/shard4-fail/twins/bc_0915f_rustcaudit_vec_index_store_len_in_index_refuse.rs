#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
// TWIN: Logos indexes by u64; Rust by usize.
fn lmain() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64);
    v.push(2i64);
    v[v.len() - 1] = 5i64;
    return (v[1] - 5i64) as i32;
}
fn main() {}
