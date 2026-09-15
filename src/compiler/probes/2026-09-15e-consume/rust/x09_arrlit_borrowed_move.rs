// RUST TWIN of x09_arrlit_borrowed_move.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
// TWIN: derive_copy/derive_clone -> #[derive(Clone, Copy)]
// TWIN: impl Add for D: added `type Output = D;`
// TWIN: impl Neg for D: added `type Output = i64;`
// TWIN: impl Add for P: added `type Output = P;`
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct W { x: D, y: D }
#[derive(Clone, Copy)]
struct P { v: i64 }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
impl Add for D { type Output = D; fn add(self, o: D) -> D { return D { v: self.v * 0i64 + 100i64, c: self.c }; } }
impl Neg for D { type Output = i64; fn neg(self) -> i64 { return 0i64 - self.v; } }
impl Add for P { type Output = P; fn add(self, o: P) -> P { return P { v: self.v + o.v }; } }
fn g(p: *mut i64) -> i64 { let a: D = D { v: 1i64, c: p }; let r: &D = &a; let arr: [D; 1] = [a]; return r.v + arr[0].v; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 1i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
