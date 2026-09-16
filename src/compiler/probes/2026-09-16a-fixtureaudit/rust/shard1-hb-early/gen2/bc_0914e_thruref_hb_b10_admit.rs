// TWIN of tests/logos/pass/bc_0914e_thruref_hb_b10_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.string;` dropped (prelude in Rust)
// TWIN: Option::Some/None -> prelude
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program b10 — caught: refuted an arm — PROBES.md "pbdbm (15 moved): ... REFUSED LEGAL b10 k01 k05 k08 (a use of the scrutinee after the binding's LAST use) and t4 t5 t6 m14 m15 (an assig...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/ctl.AXJG/b10_shared_refvar_then_mut.logos
fn __logos_main() -> i32 {
    let mut foo: Option<String> = Some(String::from("foo"));
    let bar: &mut Option<String> = &mut foo;
    match bar {
        Some(baz) => {
            let _n: i64 = baz.len() as i64;
            let _t: Option<String> = bar.take();
        }
        None => { }
    }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

