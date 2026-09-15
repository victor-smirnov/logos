// RUST TWIN of at_binding_whole_struct_wild_fields_refused.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row at_binding_whole_struct_wild_fields_refused (tier 3). `y @ D { v: _, c: _ }` over a by-value `D: Drop` scrutinee is
// refused: "cannot move out of 'D' into pattern binding 'y': type 'D' implements the Drop trait, so its destructor must see t[he whole value]".
// Legal Rust: `y` binds the WHOLE value (a move of `w`, not of a field) and the wildcard subpatterns bind nothing; E0509 is only for moving a
// FIELD out of a Drop type. Rust: y dropped at arm end (2), x at scope end (1): 21, exit 0. Found by hand program m03 of round
// 2026-09-15e-consume while measuring at_binding_by_value_never_dropped's shapes. Legality by reading; no rustc binary.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn g(p: *mut i64) -> i64 {
    let x: D = D { v: 1i64, c: p };
    let w: D = D { v: 2i64, c: p };
    let mut k: i64 = 0i64;
    match w {
        y @ D { v: _, c: _ } => { k = y.v; }
    }
    return k + x.v;
}
fn logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    if g(p) != 3i64 { return 9i32; }
    if unsafe { n } != 21i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
