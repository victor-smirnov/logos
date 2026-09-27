trait D { fn d(&self) -> i64; }
impl D for i64 { fn d(&self) -> i64 { *self * 2 } }
fn show(x: &impl D) -> i64 { x.d() }
fn main() { println!("{}", show(&4i64)); }
