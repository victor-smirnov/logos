#![allow(dead_code, unused_variables, static_mut_refs)]
static mut NDROP: i64 = 0;
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { unsafe { NDROP += 1; } } }
struct W { s: S }
enum Outer { V(W), Z }
fn inner() -> i64 {
    let x = Outer::V(W { s: S { n: 5i64 } });
    let mut out: i64 = 0;
    match &x {
        Outer::V(W { s }) => { out = s.n; },
        Outer::Z => {}
    }
    return out;
}
fn main() { let o = inner(); if o != 5 { std::process::exit(100); } unsafe { std::process::exit(NDROP as i32); } }
