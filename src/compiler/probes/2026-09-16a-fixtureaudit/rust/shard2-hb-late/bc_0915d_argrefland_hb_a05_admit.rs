// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a05_admit.logos
struct D { v: i64 }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let t: (&&D, i64) = (&rd, 1);
    let x: &D = *t.0;
    if x as *const D != want { return 5; }
    0
}
fn main() { std::process::exit(run()); }
