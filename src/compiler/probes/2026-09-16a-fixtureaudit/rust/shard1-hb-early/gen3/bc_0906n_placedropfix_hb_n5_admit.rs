// TWIN of tests/logos/pass/bc_0906n_placedropfix_hb_n5_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06n-placedropfix, program N5 — caught: verdict moved base -> landed — PROBES.md table "N1 ... base 1010 armed **1011** correct 1011 · N2 4 -> **10** · N5 1010 -> **1011** · N6 1010 -> **1011** · N7 1001 -> **1002**" (...
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06n-placedropfix/hand/N5_index_under_deref_root.logos
// N5 — an INDEX place whose container is reached THROUGH a `&mut` deref:
// `(*q).a[0] = new`. Both axes in one place expression, a shape the singles
// cannot answer. Expected 1 (old) + 1000 + 10 = 1011.
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct S { a: [D; 2] }
fn __logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    {
        let mut s: S = S { a: [D { v: 1i64, c: p }, D { v: 10i64, c: p }] };
        let q: &mut S = &mut s;
        (*q).a[0] = D { v: 1000i64, c: p };
    }
    let got: i64 = unsafe { n };
    unsafe { print!("COUNT={}\n", got); }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

