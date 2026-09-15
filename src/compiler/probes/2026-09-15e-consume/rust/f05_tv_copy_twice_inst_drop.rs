// RUST TWIN of f05_tv_copy_twice_inst_drop.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
// TWIN: derive_copy/derive_clone -> #[derive(Clone, Copy)]
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
#[derive(Clone, Copy)]
struct P { v: i64 }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn dup<T: Copy>(x: T) -> [T; 3] { return [x, x, x]; }
fn g(p: *mut i64) -> i64 { let a: [i64; 3] = dup(5i64); return a[2]; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = rd(p);
    if got != 0i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
