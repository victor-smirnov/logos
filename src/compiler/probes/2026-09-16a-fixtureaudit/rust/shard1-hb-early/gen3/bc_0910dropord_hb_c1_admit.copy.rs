// TWIN of tests/logos/pass/bc_0910dropord_hb_c1_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: by-value `str` param -> `&str` (str is unsized in Rust)
// TWIN: A16 VARIANT: #[derive(Clone, Copy)] added to 1 non-Drop struct(s)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-10dropord, program c1 — caught: verdict moved base -> landed — PROBES.md "c1 struct, 3 fields 321 -> 123 ... c17 nested struct + user Drop 3n921 -> n9123" (drop order, landed 1fad68657)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from snapshot hand-harvest-2026-09-14/25aa8421-fce1-4a11-8a89-5d2ba5981c88/dropord/cex/c1.logos
fn p(fmt: &str, v: i32) { unsafe { printf(fmt.as_ptr(), v); } }
struct O { id: i32 }
impl Drop for O { fn drop(&mut self) { p("%d", self.id); } }
#[derive(Clone, Copy)]
struct S3 { a: O, b: O, c: O }
fn __logos_main() -> i32 {
    {
        let s: S3 = S3 { a: O { id: 1 }, b: O { id: 2 }, c: O { id: 3 } };
        p("[%d]", 0);
    }
    unsafe { print!("\n"); }
    return 0;
}

fn main() { std::process::exit(__logos_main() as i32); }

