// RUST TWIN of o04_method_add_ctl.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
// TWIN: impl Add for D: added `type Output = i64;`
// TWIN: impl Neg for D: added `type Output = i64;`
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct W { x: D, y: D }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
impl Add for D { type Output = i64; fn add(self, o: D) -> i64 { return self.v + o.v; } }
impl Neg for D { type Output = i64; fn neg(self) -> i64 { return 0i64 - self.v; } }
fn g(p: *mut i64) -> i64 { let a: D = D { v: 1i64, c: p }; let b: D = D { v: 10i64, c: p }; let s: i64 = a.add(b); return s; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 11i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
