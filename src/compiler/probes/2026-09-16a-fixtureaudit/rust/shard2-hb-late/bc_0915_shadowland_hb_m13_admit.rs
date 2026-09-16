// TWIN of tests/logos/pass/bc_0915_shadowland_hb_m13_admit.logos
struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] fn eatd(x: D) -> i64 { x.v }
#[allow(dead_code)] fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn s1() -> String { String::from("abcdefgh") }
fn run() -> i32 {
    if s1().len() < 6 { return 1; }
    if s1().len() < 6 { return 2; }
    0
}
fn main() { std::process::exit(run()); }
