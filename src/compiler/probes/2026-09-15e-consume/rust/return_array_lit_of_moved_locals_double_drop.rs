// RUST TWIN of return_array_lit_of_moved_locals_double_drop.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row return_array_lit_of_moved_locals_double_drop (tier 1). LOCALS MOVED INTO A RETURNED ARRAY LITERAL
// ARE DROPPED AGAIN AT FUNCTION EXIT.
//
// `let a = C{1}; let b = C{10}; return [a, b];` then the caller drops the array: Rust runs each destructor once, 11, exit 0.
// TODAY 22, exit 1 — both locals are also dropped when mkarr returns (a double drop of each; with a heap payload this is
// a double free). Not the autoref class: the array literal's elements are MOVES, and the return path's moved-set does not
// record them. Measured on base b54c1160ae8e4066 and on the 2026-09-14o landing: 22 both.
// Found 2026-09-14o-autoreffund by hand program x01 (while explaining c10's 12376). Legality by reading; no rustc binary.
struct C { v: i64, c: *mut i64 }
impl Drop for C { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn mkarr(p: *mut i64) -> [C; 2] {
    let a: C = C { v: 1i64, c: p };
    let b: C = C { v: 10i64, c: p };
    return [a, b];
}
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let arr: [C; 2] = mkarr(p);
        if arr[1].v != 10i64 { return 3i32; }
    }
    let got: i64 = unsafe { n };
    if got != 11i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
