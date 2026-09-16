// TWIN of tests/logos/pass/bc_0908_fatrepr_hb_b4_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-08-fatrepr, program B4 — caught: refuted an arm — FINDINGS.md:129 "B4 — ... runs rc 0 at base and SEGFAULTS (rc 139) under the arm [fatbind]"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr/hand/B4.logos
// B4 (fatbind) — a `&dyn Tr` FIELD with a METHOD called through the by-value
// binder. Legal and correct today (a FatDyn field at the same door); the arm
// touches the same branch, so this is its blast-radius control.
trait Tr { fn v(&self) -> i64; }
struct A { x: i64 }
impl Tr for A { fn v(&self) -> i64 { return self.x; } }
struct H { d: &dyn Tr, n: i64 }
fn __logos_main() -> i32 {
    let a: A = A { x: 17i64 };
    let h: H = H { d: &a, n: 7i64 };
    match h {
        H { d, n } => {
            if d.v() != 17i64 { return 10i32; }
            if n != 7i64 { return 11i32; }
        }
    }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

