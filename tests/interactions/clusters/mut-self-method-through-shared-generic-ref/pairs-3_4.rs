struct C { n: i64 }
trait Counter { fn bump2(&mut self); }
impl Counter for C { fn bump2(&mut self) { self.n += 10; } }
fn g<T: Counter>(t: &T) { t.bump2(); }
fn main() { let c = C { n: 1 }; g(&c); println!("{}", c.n); }
