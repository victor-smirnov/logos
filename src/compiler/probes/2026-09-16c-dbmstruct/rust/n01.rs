#![allow(dead_code, unused_variables, unused_mut, static_mut_refs, irrefutable_let_patterns)]
static mut NDROP: i64 = 0;
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { unsafe { NDROP += 1; } } }
enum E { V { f: S }, W { f: S }, Z }
fn inner() -> i64 {
    let mut x = E::V { f: S { n: 5i64 } };
    let mut out: i64 = 0;
    match &x { E::V { f } | E::W { f } => { out = f.n; }, E::Z => {} }
    return out;
}
fn main() { let o = inner(); if o != 5 { std::process::exit(100); } unsafe { std::process::exit(NDROP as i32); } }
