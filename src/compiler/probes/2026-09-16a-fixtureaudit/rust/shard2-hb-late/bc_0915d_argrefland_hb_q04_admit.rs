// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q04_admit.logos
fn run() -> i32 {
    let o: Option<i32> = Some(5);
    let r: &Option<i32> = &o;
    let pp: &&Option<i32> = &r;
    let mut got: i32 = 0;
    match pp {
        &&Some(x) => { got = x; }
        &&None => { got = 77; }
    }
    if got != 5 { return 4; }
    0
}
fn main() { std::process::exit(run()); }
