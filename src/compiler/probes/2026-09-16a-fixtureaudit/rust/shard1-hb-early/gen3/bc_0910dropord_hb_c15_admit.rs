// TWIN of tests/logos/pass/bc_0910dropord_hb_c15_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: self: &T -> &self
// TWIN: by-value `str` param -> `&str` (str is unsized in Rust)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-10dropord, program c15 — caught: verdict moved base -> landed — PROBES.md "c1 struct, 3 fields 321 -> 123 ... c17 nested struct + user Drop 3n921 -> n9123" (drop order, landed 1fad68657)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/dropord/cex/c15.logos
fn p(fmt: &str, v: i32) { unsafe { printf(fmt.as_ptr(), v); } }
struct O { id: i32 }
impl Drop for O { fn drop(&mut self) { p("%d", self.id); } }
struct S2 { a: O, b: O }
trait Tr { fn go(&self) -> i32; }
impl Tr for S2 { fn go(&self) -> i32 { return 1; } }
fn __logos_main() -> i32 {
    {
        let d: Box<dyn Tr> = Box::new(S2 { a: O { id: 1 }, b: O { id: 2 } });
        p("[%d]", d.go());
    }
    unsafe { print!("\n"); }
    return 0;
}

fn main() { std::process::exit(__logos_main() as i32); }

