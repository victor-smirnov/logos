// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q18_admit.logos
struct P { x: i32 }
fn run() -> i32 {
    let mut p = P { x: 5 };
    let r: &mut P = &mut p;
    let pp: &&mut P = &r;
    match pp { & &mut P { x } => { if x != 5 { return 18; } } }
    0
}
fn main() { std::process::exit(run()); }
