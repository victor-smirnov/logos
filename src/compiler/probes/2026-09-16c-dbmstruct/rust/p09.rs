#![allow(dead_code, unused_variables, static_mut_refs)]
static mut NDROP: i64 = 0;
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { unsafe { NDROP += 1; } } }

fn inner() -> i64 {
    let x = (S { n: 5i64 }, 9i64);
    let mut out: i64 = 0;
    match &x {
        (s, b) => { out = s.n; }
    }
    return out;
}
fn main() { let o = inner(); if o != 5 { std::process::exit(100); } unsafe { std::process::exit(NDROP as i32); } }
