// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a06_admit.logos
struct D { v: i64 }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let arr: [&&D; 1] = [&rd];
    let x: &D = *arr[0];
    if x as *const D != want { return 6; }
    0
}
fn main() { std::process::exit(run()); }
