// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q20_admit.logos
struct P { x: i32, y: i32 }
fn run() -> i32 {
    let p = P { x: 5, y: 6 };
    let r: &P = &p;
    let pp: &&P = &r;
    let a: i32 = match pp { &q => q.y };
    if a != 6 { return 20; }
    let t: (i32, i32) = (3, 4);
    let rt: &(i32, i32) = &t;
    let ppt: &&(i32, i32) = &rt;
    let b: i32 = match ppt { &q => q.1 };
    if b != 4 { return 21; }
    0
}
fn main() { std::process::exit(run()); }
