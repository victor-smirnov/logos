// TWIN of tests/logos/pass/bc_0914e_thruref_hb_m14_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.boxed;` dropped (prelude in Rust)
// TWIN: Option::Some/None -> prelude
// TWIN: box_new(..) -> Box::new(..)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program m14 — caught: refuted an arm — PROBES.md "pbdbm (15 moved): ... REFUSED LEGAL b10 k01 k05 k08 (a use of the scrutinee after the binding's LAST use) and t4 t5 t6 m14 m15 (an assig...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/nbr2.41T7/m14_param_ref_reassign_cursor_legal.logos
struct N { v: i64, nx: Option<Box<N>> }
fn last(n: &N) -> i64 {
    let mut cur: &N = n;
    loop {
        match &cur.nx {
            Some(b) => { cur = &**b; }
            None => { return cur.v; }
        }
    }
}
fn __logos_main() -> i32 {
    let n: N = N { v: 1i64, nx: Some(Box::new(N { v: 5i64, nx: None })) };
    return (last(&n) - 5i64) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

