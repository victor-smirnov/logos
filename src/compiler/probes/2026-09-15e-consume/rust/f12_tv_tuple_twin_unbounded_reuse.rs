// RUST TWIN of f12_tv_tuple_twin_unbounded_reuse.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;

fn f<T>(x: T) -> T {
    let a: (T, i64) = (x, 1i64);
    return x;
}
fn logos_main() -> i32 {
    let q: i64 = f(7i64);
    if q != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
