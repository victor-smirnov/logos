// TWIN of tests/logos/pass/bc_0914d_meetoblland_hb_l201_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14d-meetoblland, program L201 — caught: refuted an arm — PROBES.md "L201 LEGAL refused by build 1"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/landbatm2.th4p/L201_swap_meet_static_member.logos
struct P<'a> { x: &'a i64, y: &'a i64 }
static S: i64 = 5i64;
fn same<T>(a: &mut T, b: &mut T) -> i64 { return 0i64; }
fn put(p: &mut P) -> i64 {
    let mut q = P { x: p.x, y: &S };
    return same(p, &mut q) + *q.y;
}
fn __logos_main() -> i32 {
    let a: i64 = 2i64;
    let mut p = P { x: &a, y: &a };
    return put(&mut p) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

