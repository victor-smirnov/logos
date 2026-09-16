// TWIN of tests/logos/pass/bc_0915_shadowland_hb_d09_admit.logos
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] fn eatd(x: D) -> i64 { x.v }
#[allow(dead_code)] fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn g(p: *mut i64, a: bool, b: bool) {
    let x: D = D { v: 1, c: p };
    if a { let _k: i64 = eatd(x); }
    let x: D = D { v: 2, c: p };
    if b { let _j: i64 = eatd(x); }
}
fn run() -> i32 {
    let mut n: i64 = 0; let p: *mut i64 = &mut n;
    g(p, true, false);
    if rd(p) != 12 { return 1; }
    unsafe { *p = 0; }
    g(p, false, true);
    if rd(p) != 21 { return 2; }
    0
}
fn main() { std::process::exit(run()); }
