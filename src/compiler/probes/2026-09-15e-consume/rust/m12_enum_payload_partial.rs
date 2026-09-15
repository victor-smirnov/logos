// RUST TWIN of m12_enum_payload_partial.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
struct W { x: D, y: D }
struct T(D, i64);
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
enum E { V(D, D), U }
fn g(p: *mut i64) -> i64 { let e: E = E::V(D { v: 2i64, c: p }, D { v: 3i64, c: p }); let mut k: i64 = 0i64;
    match e { E::V(z, _) => { k = z.v; } E::U => {} }
    return k; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 23i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
