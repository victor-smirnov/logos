// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q29_admit.logos
struct D { v: i64, n: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.n = *self.n + 1; } } }
fn run() -> i32 {
    let mut n: i64 = 0;
    let pn: *mut i64 = &mut n as *mut i64;
    {
        let d = D { v: 6, n: pn };
        let rd: &D = &d;
        let mut s: i64 = 0;
        match rd { q => { s = s + q.v; } }
        let rrd: &&D = &rd;
        match rrd { &q => { s = s + q.v; } }
        if s != 12 { return 29; }
    }
    (n as i32) - 1
}
fn main() { std::process::exit(run()); }
