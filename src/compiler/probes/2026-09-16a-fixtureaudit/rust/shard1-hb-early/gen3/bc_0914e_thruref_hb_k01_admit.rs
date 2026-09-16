// TWIN of tests/logos/pass/bc_0914e_thruref_hb_k01_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.string;` dropped (prelude in Rust)
// TWIN: Option::Some/None -> prelude
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program k01 — caught: refuted an arm, predicted wrong — PROBES.md "pbdbm (15 moved): ... REFUSED LEGAL b10 k01 k05 k08 (a use of the scrutinee after the binding's LAST use) and t4 t5 t6 m14 m15 (an assig...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/ctlb2.kQ72/k01_take_after_last_use.logos
fn __logos_main() -> i32 {
    let mut foo: Option<String> = Some(String::from("foo"));
    let bar: &mut Option<String> = &mut foo;
    let mut n: i64 = 0i64;
    match bar {
        Some(baz) => {
            n = baz.len() as i64;
            let _t: Option<String> = bar.take();
        }
        None => { }
    }
    return n as i32 - 3i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

