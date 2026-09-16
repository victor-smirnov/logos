// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q24_admit.logos
struct P { x: i32, y: i32 }
fn run() -> i32 {
    let p = P { x: 5, y: 6 };
    let r: &P = &p;
    match r { q => { if q.y != 6 { return 24; } } }
    let a: i32 = match r { q => q.y };
    if a != 6 { return 25; }
    let rr: &&P = &r;
    match rr { q => { if q.y != 6 { return 26; } } }
    0
}
fn main() { std::process::exit(run()); }
