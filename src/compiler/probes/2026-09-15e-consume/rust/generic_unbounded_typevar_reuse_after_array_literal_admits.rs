// RUST TWIN of generic_unbounded_typevar_reuse_after_array_literal_admits.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row generic_unbounded_typevar_reuse_after_array_literal_admits (tier 2). `fn f<T>(x: T) -> T { let a: [T; 1] = [x];
// return x; }` — `x` moved into the array and then returned — COMPILES. rustc refuses: E0382 use of moved value `x` (T carries no Copy bound).
// The array literal skips nothing here on base: it marks no element at all (row return_array_lit_of_moved_locals_double_drop). MEASURED
// 2026-09-15e-consume: arrmove (TypeVar elements skipped, as TUPLE_LIT skips them) still admits it; the same-site variant arrmovetv REFUSES it
// ("use of moved variable 'x'"). Tuple twin: generic_unbounded_typevar_reuse_after_tuple_literal_admits. Found by hand program f11. Legality by reading.
fn f<T>(x: T) -> T {
    let a: [T; 1] = [x];
    return x;
}
fn logos_main() -> i32 {
    let q: i64 = f(7i64);
    if q != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
