// TWIN of bc_0915a_refeq_hb_e25_admit.logos
fn run() -> i32 {
    let a: char = 'z'; let b: char = 'z';
    let ra: &char = &a; let rb: &char = &b;
    let x: &&char = &ra; let y: &&char = &rb;
    if x != y { return 1; }
    0
}
fn main() { std::process::exit(run()); }
