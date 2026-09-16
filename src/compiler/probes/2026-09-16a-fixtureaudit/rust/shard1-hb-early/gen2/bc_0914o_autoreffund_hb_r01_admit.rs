// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_r01_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf -> unsafe extern "C" block
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-14o-autoreffund, program r01 — caught: a droppable method-receiver temporary after a side-effecting earlier call argument was BUILT FIRST on base (216, materialize_recv_ref prepends it); source order under the landing (126) — refutes 2026-09-14n's 'a receiver is evaluated first anyway'
// legality: by reading, no rustc binary
struct D { v: i64, s: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.s = *self.s * 10i64 + self.v + 4i64; } } }
impl D { fn get(&self) -> i64 { return self.v; } }
fn mk(v: i64, s: *mut i64) -> D {
    unsafe { *s = *s * 10i64 + v; }
    return D { v: v, s: s };
}
fn side(s: *mut i64) -> i64 {
    unsafe { *s = *s * 10i64 + 1i64; }
    return 1i64;
}
fn add(a: i64, b: i64) -> i64 { return a + b; }
fn __logos_main() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let k: i64 = add(side(s), mk(2i64, s).get());
    let got: i64 = unsafe { q };
    unsafe { printf("k=%ld seq=%ld\n".as_ptr(), k, got); }
    if k != 3i64 { return 2i32; }
    if got != 126i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

