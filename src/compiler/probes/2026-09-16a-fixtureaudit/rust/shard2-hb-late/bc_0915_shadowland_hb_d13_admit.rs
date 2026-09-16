// TWIN of tests/logos/pass/bc_0915_shadowland_hb_d13_admit.logos
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] fn eatd(x: D) -> i64 { x.v }
#[allow(dead_code)] fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn g(p: *mut i64) -> i64 {
    let k: D = D { v: 1, c: p };
    let a: [i64; 2] = [4, 5];
    for k in a { if k == 4 { return 5; } }
    k.v
}
fn run() -> i32 {
    let mut n: i64 = 0; let p: *mut i64 = &mut n;
    if g(p) != 5 { return 9; }
    if rd(p) != 1 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
