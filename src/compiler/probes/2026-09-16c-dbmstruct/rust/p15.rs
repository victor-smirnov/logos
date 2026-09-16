#![allow(dead_code, unused_variables, unused_mut, static_mut_refs)]
static mut NDROP: i64 = 0;
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { unsafe { NDROP += 1; } } }
enum O3 { V((S, i64)), Z }
fn inner() -> i64 {
    let mut x = O3::V((S { n: 5i64 }, 1i64));
    let mut out: i64 = 0;
    match &x { O3::V((s, k)) => { out = s.n; }, O3::Z => {} }
    return out;
}
fn main() { let o = inner(); if o != 5 { std::process::exit(100); } unsafe { std::process::exit(NDROP as i32); } }
