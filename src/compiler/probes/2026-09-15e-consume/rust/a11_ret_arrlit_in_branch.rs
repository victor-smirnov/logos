// RUST TWIN of a11_ret_arrlit_in_branch.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct W { x: D, y: D }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn mk(p: *mut i64, k: bool) -> [D; 2] { let a: D = D { v: 1i64, c: p }; let b: D = D { v: 10i64, c: p }; if k { return [a, b]; } return [D { v: 100i64, c: p }, D { v: 1000i64, c: p }]; }
fn g(p: *mut i64) -> i64 { { let arr: [D; 2] = mk(p, true); if arr[1].v != 10i64 { return 7i64; } } return 0i64; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 11i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
