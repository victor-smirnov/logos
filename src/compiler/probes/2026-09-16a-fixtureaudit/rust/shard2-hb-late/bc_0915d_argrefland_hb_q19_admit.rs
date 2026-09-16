// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q19_admit.logos
struct P { x: i32, y: i32 }
impl Copy for P {}
impl Clone for P { fn clone(&self) -> P { P { x: self.x, y: self.y } } }
fn run() -> i32 {
    let p = P { x: 5, y: 6 };
    let r: &P = &p;
    let pp: &&P = &r;
    match pp { &&q => { if q.y != 6 { return 19; } } }
    match pp { &q => { if q.y != 6 { return 20; } } }
    0
}
fn main() { std::process::exit(run()); }
