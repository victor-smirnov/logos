

trait Counter { fn bump(&self) -> i64; }
struct S(i64);
impl Counter for S { fn bump(&self) -> i64 { self.0 + 1 } }
fn bt(c: &impl Counter) -> i64 { c.bump() }
fn main() { let s = S(4); println!("{}", bt(&s)); }
