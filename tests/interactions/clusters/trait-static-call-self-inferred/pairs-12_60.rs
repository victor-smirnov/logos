#[derive(Default)]
struct Cfg { w: i64, h: i64 }
fn main() { let c = Cfg { w: 2, ..Default::default() }; let d: Cfg = Default::default(); println!("{} {} {}", c.w, c.h, d.w); }
