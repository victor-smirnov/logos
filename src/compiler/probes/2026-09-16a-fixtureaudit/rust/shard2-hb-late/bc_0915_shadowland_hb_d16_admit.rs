// TWIN of tests/logos/pass/bc_0915_shadowland_hb_d16_admit.logos
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] fn eatd(x: D) -> i64 { x.v }
#[allow(dead_code)] fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn run() -> i32 {
    let mut n: i64 = 0; let p: *mut i64 = &mut n;
    {
        let x: D = D { v: 1, c: p };
        let x: D = D { v: 2, c: p };
        let _k: i64 = eatd(x);
        let x: D = D { v: 3, c: p };
        if x.v != 3 { return 9; }
    }
    if rd(p) != 231 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
