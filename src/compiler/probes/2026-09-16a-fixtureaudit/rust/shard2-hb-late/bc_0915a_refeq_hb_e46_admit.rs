// TWIN of bc_0915a_refeq_hb_e46_admit.logos
fn run() -> i32 {
    let a = String::from("left"); let b = String::from("right"); let c = String::from("left");
    let ra: &String = &a; let rb: &String = &b; let rc: &String = &c;
    if !(ra != rb) { return 1; }
    if ra != rc { return 2; }
    0
}
fn main() { std::process::exit(run()); }
