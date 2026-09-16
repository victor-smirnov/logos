// TWIN of bc_0915a_refeq_hb_e30_admit.logos
fn run() -> i32 {
    let s2: String = String::from("ab");
    let a = "ab";
    let b = s2.as_str();
    let x = &a; let y = &b;
    if x == y { return 0; }
    1
}
fn main() { std::process::exit(run()); }
