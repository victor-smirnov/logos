// RUST TWIN of fill_n0.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let a: D = D { v: 1i64, c: p };
        let arr: [D; 0] = [a; 0];
    }
    let got: i64 = unsafe { n };
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
