// TWIN of bc_0915a_refeq_hb_e09_admit.logos
fn run() -> i32 {
    let a: (i64, i64) = (1, 2); let b: (i64, i64) = (1, 2);
    let ra: &(i64, i64) = &a; let rb: &(i64, i64) = &b;
    if ra == rb { return 0; }
    1
}
fn main() { std::process::exit(run()); }
