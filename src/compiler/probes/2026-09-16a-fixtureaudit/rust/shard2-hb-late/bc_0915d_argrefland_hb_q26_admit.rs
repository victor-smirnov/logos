// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q26_admit.logos
struct P { x: i64, y: i64 }
fn run() -> i32 {
    let p = P { x: 5, y: 6 };
    let r: &P = &p;
    let rr: &&P = &r;
    let rrr: &&&P = &rr;
    let v: i64 = match rrr { P { x, y } => *x * 10 + *y };
    if v != 56 { return 26; }
    let t: (i64, i64) = (1, 2);
    let rt: &(i64, i64) = &t;
    let rrt: &&(i64, i64) = &rt;
    let w: i64 = match rrt { (a, b) => *a + *b };
    if w != 3 { return 27; }
    0
}
fn main() { std::process::exit(run()); }
