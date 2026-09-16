// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q16_admit.logos
struct P { x: i32, n: *mut i64 }
impl Drop for P { fn drop(&mut self) { unsafe { *self.n = *self.n + 1; } } }
fn run() -> i32 {
    let mut n: i64 = 0;
    let pn: *mut i64 = &mut n as *mut i64;
    {
        let p = P { x: 5, n: pn };
        let r: &P = &p;
        let pp: &&P = &r;
        let mut s: i32 = 0;
        match pp { &&P { x, n: _ } => { s = s + x; } }
        let t: i32 = match pp { &&P { x, n: _ } => x };
        if s + t != 10 { return 16; }
    }
    (n as i32) - 1
}
fn main() { std::process::exit(run()); }
