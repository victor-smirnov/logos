// TWIN of tests/logos/pass/bc_0915_shadowland_hb_d28_admit.logos
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] fn eatd(x: D) -> i64 { x.v }
#[allow(dead_code)] fn rd(p: *mut i64) -> i64 { unsafe { *p } }
struct H { a: D, b: D }
fn run() -> i32 {
    let mut n: i64 = 0; let p: *mut i64 = &mut n;
    {
        let h: H = H { a: D { v: 1, c: p }, b: D { v: 2, c: p } };
        {
            let h: H = H { a: D { v: 3, c: p }, b: D { v: 4, c: p } };
            let _k: i64 = eatd(h.a);
        }
        if h.a.v != 1 { return 9; }
    }
    if rd(p) != 3412 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
