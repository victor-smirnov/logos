// TWIN of tests/logos/pass/bc_0906n_placedropfix_hb_n7_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf -> unsafe extern "C" block
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-06n-placedropfix, program N7 — caught: verdict moved base -> landed — PROBES.md table "N1 ... base 1010 armed **1011** correct 1011 · N2 4 -> **10** · N5 1010 -> **1011** · N6 1010 -> **1011** · N7 1001 -> **1002**" (...
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06n-placedropfix/hand/N7_rhs_reads_the_old_value.logos
// N7 — the RHS READS the place it is about to overwrite:
// `t.0 = D { v: t.0.v + 1000, c: p }`. If the old value were dropped BEFORE the
// RHS is evaluated the read would be of a destroyed value. Expected
// 1 (old) + 1001 (new) = 1002.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn __logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let mut t: (D, i64) = (D { v: 1i64, c: p }, 0i64);
        t.0 = D { v: t.0.v + 1000i64, c: p };
    }
    let got: i64 = unsafe { n };
    unsafe { printf("COUNT=%ld\n".as_ptr(), got); }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

