// TWIN of tests/logos/pass/bc_0914e_thruref_hb_t5_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program t5 — caught: refuted an arm — PROBES.md "pbdbm (15 moved): ... REFUSED LEGAL b10 k01 k05 k08 (a use of the scrutinee after the binding's LAST use) and t4 t5 t6 m14 m15 (an assig...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/walk.ek13/t5_enum_door_mut_reassign_refvar.logos
fn __logos_main() -> i32 {
    let mut s1: Option<i64> = Some(1i64);
    let mut s2: Option<i64> = Some(3i64);
    let mut r: &mut Option<i64> = &mut s1;
    match r {
        Some(a) => {
            r = &mut s2;
            *a = 7i64;
        }
        None => { }
    }
    match s1 { Some(v) => { return (v - 7i64) as i32; } None => { return 9i32; } }
}

fn main() { std::process::exit(__logos_main() as i32); }

