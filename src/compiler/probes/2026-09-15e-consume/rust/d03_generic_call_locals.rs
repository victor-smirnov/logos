// RUST TWIN of d03_generic_call_locals.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
// TWIN: impl Add for D: added `type Output = D;`
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn eat(d: D) -> i64 { return d.v * 0i64; }
impl Add for D { type Output = D; fn add(self, o: D) -> D { return D { v: 100i64, c: self.c }; } }
trait Tk { fn take(&self, d: D) -> i64; }
struct K { k: i64 }
impl Tk for K { fn take(&self, d: D) -> i64 { return d.v * 0i64 + self.k; } }
impl K { fn mk(d: D) -> K { return K { k: d.v * 0i64 }; } }
fn two<T>(x: T, y: T) -> i64 { return 0i64; }
fn g(p: *mut i64) -> i64 { let a: D = D { v: 1i64, c: p }; let b: D = D { v: 10i64, c: p }; return two(a, b); }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 11i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
