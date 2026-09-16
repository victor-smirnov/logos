// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_h19_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.string;` dropped (prelude in Rust)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14o-autoreffund, program h19 — caught: (round 2026-09-14n) `String == String` with temporaries: 68 bytes definitely lost on base (valgrind), 0 errors under the landing
// legality: by reading, no rustc binary
fn __logos_main() -> i32 {
    let s: String = String::from("hello-world-long");
    if !(s == String::from("hello-world-long")) { return 3i32; }
    if s != String::from("hello-world-lonG") { return 0i32; }
    return 4i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

