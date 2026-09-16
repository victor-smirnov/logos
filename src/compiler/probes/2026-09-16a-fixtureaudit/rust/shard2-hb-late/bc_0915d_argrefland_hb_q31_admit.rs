// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q31_admit.logos
fn get(pp: &&Option<i64>) -> i64 {
    match pp {
        &&Some(x) => x,
        other => { let o2: &&Option<i64> = other; if o2 == pp { 77 } else { 66 } }
    }
}
fn run() -> i32 {
    let o: Option<i64> = Some(5);
    let r: &Option<i64> = &o;
    if get(&r) != 5 { return 31; }
    let n: Option<i64> = None;
    let rn: &Option<i64> = &n;
    if get(&rn) != 77 { return 32; }
    0
}
fn main() { std::process::exit(run()); }
