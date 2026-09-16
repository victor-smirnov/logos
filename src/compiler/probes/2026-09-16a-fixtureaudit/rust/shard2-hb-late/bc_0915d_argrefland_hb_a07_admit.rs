// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_a07_admit.logos
struct D { v: i64 }
fn run() -> i32 {
    let d = D { v: 6 };
    let rd: &D = &d;
    let want: *const D = rd as *const D;
    let o: Option<&&D> = Some(&rd);
    match o {
        Some(p) => {
            let x: &D = *p;
            if x as *const D != want { return 7; }
        }
        None => { return 70; }
    }
    0
}
fn main() { std::process::exit(run()); }
