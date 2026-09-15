// RUST TWIN of e07_addassign_temp_rhs.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
// TWIN: impl Neg for D: added `type Output = D;`
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct W { x: D, y: D }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
impl Neg for D { type Output = D; fn neg(self) -> D { return D { v: 100i64, c: self.c }; } }
impl AddAssign for D { fn add_assign(&mut self, o: D) { self.v = self.v + 0i64 * o.v; } }
trait Tr { fn get(&self) -> i64; }
impl Tr for D { fn get(&self) -> i64 { return self.v; } }
fn g(p: *mut i64) -> i64 { let mut acc: D = D { v: 1i64, c: p }; acc += D { v: 10i64, c: p }; return acc.v; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 11i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
