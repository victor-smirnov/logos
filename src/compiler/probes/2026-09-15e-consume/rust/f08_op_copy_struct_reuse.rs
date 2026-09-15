// RUST TWIN of f08_op_copy_struct_reuse.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
// TWIN: derive_copy/derive_clone -> #[derive(Clone, Copy)]
// TWIN: impl Add for P: added `type Output = P;`
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
#[derive(Clone, Copy)]
struct P { v: i64 }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
impl Add for P { type Output = P; fn add(self, o: P) -> P { return P { v: self.v + o.v }; } }
fn g(p: *mut i64) -> i64 { let a: P = P { v: 1i64 }; let b: P = a + a; let c: P = a + b; return c.v; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 0i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
