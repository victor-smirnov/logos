trait Counter { fn val(&self) -> i64; }
struct C { n: i64 }
impl Counter for C { fn val(&self) -> i64 { return self.n; } }
fn f(c: &impl Counter) -> i64 { return c.val(); }
fn main() { println!("{}", f(&C { n: 3 })); }
