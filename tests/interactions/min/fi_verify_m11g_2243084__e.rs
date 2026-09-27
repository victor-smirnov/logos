struct Cfg { w: i64, h: i64 }
impl Default for Cfg { fn default() -> Cfg { Cfg { w: 1, h: 7 } } }
fn main() { let c = Cfg { w: 5, ..Default::default() }; std::process::exit((c.w + c.h) as i32); }
