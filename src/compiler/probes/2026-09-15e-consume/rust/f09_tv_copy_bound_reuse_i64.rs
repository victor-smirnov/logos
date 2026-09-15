// RUST TWIN of f09_tv_copy_bound_reuse_i64.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

fn f<T: Copy>(x: T) -> T {
    let a: [T; 2] = [x, x];
    let b: [T; 1] = [x];
    return x;
}
fn logos_main() -> i32 {
    let q: i64 = f(7i64);
    if q != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
