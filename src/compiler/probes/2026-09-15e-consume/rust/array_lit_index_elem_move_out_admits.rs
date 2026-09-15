// RUST TWIN of array_lit_index_elem_move_out_admits.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row array_lit_index_elem_move_out_admits (tier 1, admits). `[src[0]]` MOVES A DROP ELEMENT OUT OF AN ARRAY BY INDEX,
// IS ADMITTED, AND DESTROYS IT TWICE (n = 2 today). rustc refuses: E0508 "cannot move out of type `[D; 1]`, a non-copy array" — the same
// sentence SemaChecker::mark_moved_expr's IndexRead arm already gives `let s = src[0];`. The array literal never asks: lower_arr_lit does not
// hand its elements to mark_moved_expr. MEASURED 2026-09-15e-consume: under arrmove / consumex (the element loop marks each element consumed)
// this program is REFUSED with exactly that sentence — it closes with arrmove if arrmove lands. Found by hand program e01. Legality by reading.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let src: [D; 1] = [D { v: 1i64, c: p }];
        let arr: [D; 1] = [src[0]];
        if arr[0].v != 1i64 { return 7i32; }
    }
    let got: i64 = unsafe { n };
    if got != 1i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
