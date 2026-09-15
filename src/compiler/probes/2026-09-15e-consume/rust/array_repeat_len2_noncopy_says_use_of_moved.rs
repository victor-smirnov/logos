// RUST TWIN of array_repeat_len2_noncopy_says_use_of_moved.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row array_repeat_len2_noncopy_says_use_of_moved (tier 4, diag). `[a; 2]` over a non-Copy `D` is refused — rightly —
// with the wrong sentence: logosc says "use of moved value 'a' (moved on line 8)"; rustc says E0277 "the trait bound `D: Copy` is not
// satisfied" (tests/ui/repeat-expr/repeat-to-run-dtor-twice.stderr: "the `Copy` trait is required because this value will be copied for
// each element of the array"). Found beside array_repeat_len1_noncopy_operand_double_drop_run, round 2026-09-15e-consume.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let a: D = D { v: 1i64, c: p };
    let arr: [D; 2] = [a; 2];
    return arr[0].v as i32;
}

fn main() { std::process::exit(logos_main()) }
