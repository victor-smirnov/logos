// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q04e_admit.logos
fn run() -> i32 {
    let o: Option<i32> = Some(5);
    let r: &Option<i32> = &o;
    let pp: &&Option<i32> = &r;
    let got: i32 = match pp {
        &&Some(x) => x,
        &&None => 77,
    };
    if got != 5 { return 4; }
    0
}
fn main() { std::process::exit(run()); }
