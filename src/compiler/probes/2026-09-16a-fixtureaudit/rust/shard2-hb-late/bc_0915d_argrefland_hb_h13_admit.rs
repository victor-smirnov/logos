// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_h13_admit.logos
struct D { v: i64, n: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.n = *self.n + 1; } } }
fn inner(a: &&D) -> i64 { let x: &D = *a; x.v }
fn run() -> i32 {
    let mut n: i64 = 0;
    let pn: *mut i64 = &mut n as *mut i64;
    {
        let o: Option<D> = Some(D { v: 6, n: pn });
        let mut s: i64 = 0;
        match o {
            Some(ref rd) => {
                s = s + inner(&rd);
                s = s + inner(&rd);
            }
            None => { return 90; }
        }
        if s != 12 { return 13; }
    }
    (n as i32) - 1
}
fn main() { std::process::exit(run()); }
