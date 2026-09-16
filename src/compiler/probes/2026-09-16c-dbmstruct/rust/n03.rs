#![allow(dead_code, unused_variables, unused_mut, static_mut_refs, irrefutable_let_patterns)]
static mut NDROP: i64 = 0;
struct S { pub n: i64 }
impl Drop for S { fn drop(&mut self) { unsafe { NDROP += 1; } } }
enum Inn { S { f: S }, N }
enum Outer { V(Inn), Z }
fn inner() -> i64 {
    let mut x = Outer::V(Inn::S { f: S { n: 5i64 } });
    let mut out: i64 = 0;
    match &x { Outer::V(Inn::S { f }) => { out = f.n; }, _ => {} }
    return out;
}
fn main() { let o = inner(); if o != 5 { std::process::exit(100); } unsafe { std::process::exit(NDROP as i32); } }
