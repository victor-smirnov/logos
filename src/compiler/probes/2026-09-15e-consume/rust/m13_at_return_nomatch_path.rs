// RUST TWIN of m13_at_return_nomatch_path.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
struct W { x: D, y: D }
struct T(D, i64);
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn g(p: *mut i64) -> i64 { let x: D = D { v: 1i64, c: p }; let o: Option<D> = Option::Some(D { v: 2i64, c: p });
    match o { y @ Option::Some(_) => { return 4i64; } Option::None => {} }
    return x.v; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 21i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
