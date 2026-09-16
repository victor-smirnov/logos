// TWIN of tests/logos/pass/bc_0906n_placedropfix_hb_n2_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06n-placedropfix, program N2 — caught: verdict moved base -> landed — PROBES.md table "N1 ... base 1010 armed **1011** correct 1011 · N2 4 -> **10** · N5 1010 -> **1011** · N6 1010 -> **1011** · N7 1001 -> **1002**" (...
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06n-placedropfix/hand/N2_tuple_elem_assigned_in_loop.logos
// N2 — the SAME tuple element assigned three times in a loop. An over-drop
// (dropping a place that is already gone) or a missed drop both change the
// digit; a single-assignment program cannot tell a per-store drop from a
// once-only one. Expected: 1 + 2 + 3 (the three olds) + 4 (the survivor) = 10.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn __logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let mut t: (D, i64) = (D { v: 1i64, c: p }, 0i64);
        let mut i: i64 = 2i64;
        while i < 5i64 {
            t.0 = D { v: i, c: p };
            i = i + 1i64;
        }
    }
    let got: i64 = unsafe { n };
    unsafe { print!("COUNT={}\n", got); }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

