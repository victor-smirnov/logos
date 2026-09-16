// TWIN of tests/logos/pass/bc_0915_shadowland_hb_c01_admit.logos
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] fn eatd(x: D) -> i64 { x.v }
#[allow(dead_code)] fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn g(p: *mut i64) -> i64 {
    let i: D = D { v: 1, c: p };
    for i in 0..3i64 { if i == 0 { return 5; } }
    i.v
}
fn run() -> i32 {
    let mut n: i64 = 0; let p: *mut i64 = &mut n;
    if g(p) != 5 { return 9; }
    if rd(p) != 1 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
