// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q25_admit.logos
fn get(pp: &&Option<i64>) -> i64 {
    let mut out: i64 = 0;
    match pp {
        &&Some(x) => { out = x; }
        _ => { out = 77; }
    }
    out
}
fn run() -> i32 {
    let o: Option<i64> = Some(5);
    let r: &Option<i64> = &o;
    if get(&r) != 5 { return 25; }
    let n: Option<i64> = None;
    let rn: &Option<i64> = &n;
    if get(&rn) != 77 { return 26; }
    0
}
fn main() { std::process::exit(run()); }
