// RUST TWIN of loop_break_value_array_lit_uninit_drop_run.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row loop_break_value_array_lit_uninit_drop_run (tier 1, run 139). `let arr: [D; 1] = loop { break [a]; };` SEGFAULTS.
// valgrind: 2x "Use of uninitialised value of size 8" at D__drop called from main's scope exit. Rust: a moved into the array, one drop: 1, exit 0.
// NOT the array-literal consumption fact of round 2026-09-15e-consume: arrmove / consumex leave it at 139 with the same valgrind records
// (measured), so the break-value path loses or mis-addresses the array value before any drop accounting. Found by hand program e11. Legality by reading.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let a: D = D { v: 1i64, c: p };
        let arr: [D; 1] = loop { break [a]; };
        if arr[0].v != 1i64 { return 7i32; }
    }
    let got: i64 = unsafe { n };
    if got != 1i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
