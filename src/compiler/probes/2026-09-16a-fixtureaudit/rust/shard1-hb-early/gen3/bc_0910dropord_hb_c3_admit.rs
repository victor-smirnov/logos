// TWIN of tests/logos/pass/bc_0910dropord_hb_c3_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: by-value `str` param -> `&str` (str is unsized in Rust)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-10dropord, program c3 — caught: verdict moved base -> landed — PROBES.md "c1 struct, 3 fields 321 -> 123 ... c17 nested struct + user Drop 3n921 -> n9123" (drop order, landed 1fad68657)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/dropord/cex/c3.logos
fn p(fmt: &str, v: i32) { unsafe { printf(fmt.as_ptr(), v); } }
struct O { id: i32 }
impl Drop for O { fn drop(&mut self) { p("%d", self.id); } }
struct W { t: (O, O), u: (O, O) }
fn __logos_main() -> i32 {
    {
        let w: W = W { t: (O { id: 1 }, O { id: 2 }), u: (O { id: 3 }, O { id: 4 }) };
        p("[%d]", 0);
    }
    unsafe { print!("\n"); }
    return 0;
}

fn main() { std::process::exit(__logos_main() as i32); }

