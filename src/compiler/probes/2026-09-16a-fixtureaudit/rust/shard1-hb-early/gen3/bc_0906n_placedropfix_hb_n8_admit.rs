// TWIN of tests/logos/pass/bc_0906n_placedropfix_hb_n8_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06n-placedropfix, program N8 — caught: verdict moved base -> landed — PROBES.md "N8 heap `String` payload — the oracle is VALGRIND | 6 allocs / **4** frees, 20 B lost | 6 allocs / **6** frees, 0 errors"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06n-placedropfix/hand/N8_heap_payload_valgrind.logos
// N8 — a DIFFERENT ORACLE. The element payload is a heap `String`, so the
// evidence is valgrind's alloc/free ledger, not an arithmetic counter: a
// counter that is right can still sit on top of a leaked block. Prints the
// lengths so the program also proves it read the new value.
fn __logos_main() -> i32 {
    let mut t: (String, i64) = (String::from("aaaa"), 1i64);
    t.0 = String::from("bbbbbbbb");
    let mut a: [String; 2] = [String::from("cccc"), String::from("dddd")];
    a[0] = String::from("eeeeeeee");
    let s: i64 = (t.0.len() as i64 + a[0].len() as i64) as i64;
    unsafe { print!("COUNT={}\n", s); }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

