// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_c07_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.lang.cmp;` dropped (prelude in Rust)
// TWIN: `use logos.mem.string;` dropped (prelude in Rust)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14o-autoreffund, program c07 — caught: four `String` operator temporaries (`==`, `!=`, both sides, `str == String`): 216 bytes definitely lost in 4 blocks on base (valgrind), 0 errors under the landing
// legality: by reading, no rustc binary
fn __logos_main() -> i32 {
    let a: String = String::from("abcdefghijklmnopqrstuvwxyz");
    if !(a == String::from("abcdefghijklmnopqrstuvwxyz")) { return 3i32; }
    if a != String::from("abcdefghijklmnopqrstuvwxyz") { return 4i32; }
    if !(String::from("abcdefghijklmnopqrstuvwxyz") == a) { return 5i32; }
    if !("abcdefghijklmnopqrstuvwxyz" == String::from("abcdefghijklmnopqrstuvwxyz")) { return 6i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

