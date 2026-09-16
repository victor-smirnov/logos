#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct H<'a> { r: &'a i64 }
fn lmain() -> i32 {
    let mut v: Vec<i64> = Vec::new();
    v.push(1i64); v.push(2i64);
    let h: H = H { r: &v[0] };
    v[1] = 2i64;
    return *h.r as i32;
}
fn main() {}
