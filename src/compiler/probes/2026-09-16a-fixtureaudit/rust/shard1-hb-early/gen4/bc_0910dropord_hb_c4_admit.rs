// TWIN of tests/logos/pass/bc_0910dropord_hb_c4_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf removed from body
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: by-value `str` param -> `&str` (str is unsized in Rust)
// TWIN: dynamic-format printf kept; string literals NUL-terminated
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-10dropord, program c4 — caught: verdict moved base -> landed — PROBES.md "c1 struct, 3 fields 321 -> 123 ... c17 nested struct + user Drop 3n921 -> n9123\0" (drop order, landed 1fad68657)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/dropord/cex/c4.logos
fn p(fmt: &str, v: i32) { unsafe { printf(fmt.as_ptr(), v); } }
struct O { id: i32 }
impl Drop for O { fn drop(&mut self) { p("%d\0", self.id); } }
struct U { a: O, b: O }
impl Drop for U { fn drop(&mut self) { p("u%d\0", 9); } }
fn __logos_main() -> i32 {
    {
        let u: U = U { a: O { id: 1 }, b: O { id: 2 } };
        p("[%d]\0", 0);
    }
    unsafe { print!("\n"); }
    return 0;
}

fn main() { std::process::exit(__logos_main() as i32); }

