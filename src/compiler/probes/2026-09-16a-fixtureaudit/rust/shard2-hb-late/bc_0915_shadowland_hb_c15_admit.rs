// TWIN of tests/logos/pass/bc_0915_shadowland_hb_c15_admit.logos
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] fn eatd(x: D) -> i64 { x.v }
#[allow(dead_code)] fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn none() -> Option<i64> { None }
fn g(p: *mut i64) -> Option<i64> {
    let x: D = D { v: 1, c: p };
    let x: D = D { v: 2, c: p };
    let v: i64 = none()?;
    Some(v + x.v)
}
fn run() -> i32 {
    let mut n: i64 = 0; let p: *mut i64 = &mut n;
    let r: Option<i64> = g(p);
    if r.is_some() { return 9; }
    if rd(p) != 21 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
