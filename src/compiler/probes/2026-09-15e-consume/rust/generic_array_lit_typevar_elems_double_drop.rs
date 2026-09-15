// RUST TWIN of generic_array_lit_typevar_elems_double_drop.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row generic_array_lit_typevar_elems_double_drop (tier 1). `[x, y]` WITH `x, y: T` INSTANTIATED WITH A DROP TYPE DESTROYS
// EACH ELEMENT TWICE. `fn two<T>(x: T, y: T) -> [T; 2] { return [x, y]; }` over D: Rust 11, exit 0. TODAY 22, exit 1. The TUPLE twin `(x, y)`
// is right (11). NEIGHBOUR of 2026-09-15e-consume's arrmove, which skips TypeVar elements exactly as TUPLE_LIT does; the same-site variant
// arrmovetv (TypeVar elements marked too) closes it (measured 1 -> 0). REASON: see PROBES.md 2026-09-15e-consume arrmovetv.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn two<T>(x: T, y: T) -> [T; 2] { return [x, y]; }
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let arr: [D; 2] = two(D { v: 1i64, c: p }, D { v: 10i64, c: p });
        if arr[1].v != 10i64 { return 7i32; }
    }
    let got: i64 = unsafe { n };
    if got != 11i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
