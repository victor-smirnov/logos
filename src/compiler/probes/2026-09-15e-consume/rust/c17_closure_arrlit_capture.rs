// RUST TWIN of c17_closure_arrlit_capture.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
// TWIN: impl Add for D: added `type Output = D;`
// TWIN: impl Not for D: added `type Output = i64;`
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct H { arr: [D; 2] }
struct W { x: D, y: D }
enum E { V(D, D), U }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn eat(d: D) -> i64 { return d.v * 0i64; }
fn eat2(a: [D; 2]) -> i64 { return a[0].v * 0i64; }
impl Add for D { type Output = D; fn add(self, o: D) -> D { return D { v: 100i64, c: self.c }; } }
impl Not for D { type Output = i64; fn not(self) -> i64 { return self.v; } }
impl AddAssign for D { fn add_assign(&mut self, o: D) { self.v = self.v + 0i64 * o.v; } }
fn g(p: *mut i64) -> i64 { let a: D = D { v: 1i64, c: p }; let f = move || -> i64 { let arr: [D; 1] = [a]; return arr[0].v; }; return f(); }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 1i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
