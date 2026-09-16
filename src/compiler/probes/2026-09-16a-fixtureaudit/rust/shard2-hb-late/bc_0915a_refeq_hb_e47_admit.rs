// TWIN of bc_0915a_refeq_hb_e47_admit.logos
fn run() -> i32 {
    let a: (i64, i64) = (1, 9); let b: (i64, i64) = (1, 9);
    let ra: &(i64, i64) = &a; let rb: &(i64, i64) = &b;
    if ra < rb { return 1; }
    if !(ra <= rb) { return 2; }
    if !(rb >= ra) { return 3; }
    0
}
fn main() { std::process::exit(run()); }
