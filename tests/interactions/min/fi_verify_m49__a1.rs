trait T { fn v(&self) -> i64; }
struct S { x: i64 }
impl T for S { fn v(&self) -> i64 { self.x } }
fn ap(f: &impl T) -> i64 { f.v() }
fn main() { let s = S { x: 5 }; println!("{}", ap(&s)); }
