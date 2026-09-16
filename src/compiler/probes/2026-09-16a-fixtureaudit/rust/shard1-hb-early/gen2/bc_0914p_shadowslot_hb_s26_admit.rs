// TWIN of tests/logos/pass/bc_0914p_shadowslot_hb_s26_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.string;` dropped (prelude in Rust)
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 0914p_shadowslot, program s26 — caught: verdict moved base -> shadowslot landing
// legality: by reading, no rustc binary
fn __logos_main() -> i32 {
    let s: String = String::from("abcdefghijklmnopqrstuvwxyz0123");
    let s: String = String::from("zyxwvutsrqponmlkjihgfedcba9876");
    if s.len() as i64 != 30i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

