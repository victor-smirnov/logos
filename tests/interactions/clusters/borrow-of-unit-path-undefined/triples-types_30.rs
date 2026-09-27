struct U;
impl U { fn v(&self) -> i64 { 9 } }
fn main() { let u = U; let r = &U; println!("{} {}", u.v(), r.v()); }
