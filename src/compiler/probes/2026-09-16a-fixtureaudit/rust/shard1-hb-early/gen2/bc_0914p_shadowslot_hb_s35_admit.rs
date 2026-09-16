// TWIN of tests/logos/pass/bc_0914p_shadowslot_hb_s35_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 0914p_shadowslot, program s35 — caught: verdict moved base -> shadowslot landing
// legality: by reading, no rustc binary
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10i64 + self.v; } } }
fn eatd(x: D) -> i64 { return x.v; }
fn rd(p: *mut i64) -> i64 { return unsafe { *p }; }
fn g(x: D, p: *mut i64) -> i64 {
    let x: D = D { v: 2i64, c: p };
    return eatd(x);
}
fn __logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    if g(D { v: 1i64, c: p }, p) != 2i64 { return 9i32; }
    if rd(p) != 21i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

