// RUST TWIN of a03_ret_tuplit.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct W { x: D, y: D }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn mk(p: *mut i64) -> (D, D) { let a: D = D { v: 1i64, c: p }; let b: D = D { v: 10i64, c: p }; return (a, b); }
fn g(p: *mut i64) -> i64 { { let t: (D, D) = mk(p); if t.1.v != 10i64 { return 7i64; } } return 0i64; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 11i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
