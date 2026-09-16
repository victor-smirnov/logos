// TWIN of tests/logos/pass/bc_0915d_argrefland_hb_q28_admit.logos
struct P { x: i64, y: i64 }
impl P { fn sum(&self) -> i64 { self.x + self.y } }
fn inner(a: &&P) -> i64 { a.y }
fn run() -> i32 {
    let t: (i64, i64) = (3, 4);
    let rt: &(i64, i64) = &t;
    match rt { q => { if q.1 != 4 { return 28; } } }
    let p = P { x: 5, y: 6 };
    let r: &P = &p;
    let g: i64 = match r { q if q.x > 0 => q.sum(), _ => 0 };
    if g != 11 { return 29; }
    match r { q => { if inner(&q) != 6 { return 30; } } }
    0
}
fn main() { std::process::exit(run()); }
