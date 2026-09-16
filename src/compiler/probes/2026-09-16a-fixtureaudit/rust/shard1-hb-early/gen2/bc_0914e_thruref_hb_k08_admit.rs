// TWIN of tests/logos/pass/bc_0914e_thruref_hb_k08_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program k08 — caught: refuted an arm, predicted wrong — PROBES.md "pbdbm (15 moved): ... REFUSED LEGAL b10 k01 k05 k08 (a use of the scrutinee after the binding's LAST use) and t4 t5 t6 m14 m15 (an assig...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/ctlb2.kQ72/k08_copy_out_then_write.logos
fn __logos_main() -> i32 {
    let mut foo: Option<i64> = Some(3i64);
    let bar: &mut Option<i64> = &mut foo;
    let mut got: i64 = 0i64;
    match bar {
        Some(baz) => {
            got = *baz;
            *bar = None;
        }
        None => { }
    }
    return (got - 3i64) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

