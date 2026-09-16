// TWIN of bc_0915a_refeq_hb_e07_admit.logos
fn run() -> i32 {
    let a: String = String::from("abc"); let b: String = String::from("abc");
    let ra: &String = &a; let rb: &String = &b;
    if ra == rb { return 0; }
    1
}
fn main() { std::process::exit(run()); }
