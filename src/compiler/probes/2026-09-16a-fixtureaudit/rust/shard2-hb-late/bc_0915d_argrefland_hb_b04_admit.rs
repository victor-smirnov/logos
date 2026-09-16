// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_b04_admit.logos
struct D { v: i64, n: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.n = *self.n + 1; } } }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let mut n: i64 = 0;
    let pn: *mut i64 = &mut n as *mut i64;
    {
        let d = D { v: 6, n: pn };
        let rd: &D = &d;
        let mut i: i64 = 0;
        let mut s: i64 = 0;
        while i < 3 { s = s + inner(&rd); i = i + 1; }
        if s != 18 { return 4; }
    }
    (n as i32) - 1
}
fn main() { std::process::exit(run()); }
