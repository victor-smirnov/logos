struct Acc { total: i64 }
impl Acc {
    fn add(&mut self, v: i64) { (*self).total = (*self).total + v; }
    fn cur(&self) -> i64 { return (*self).total; }
}
fn main() {
    let mut a: Acc = Acc { total: 5i64 };
    a.add(a.cur());
    if a.total != 10i64 { std::process::exit(1); }
    std::process::exit(0);
}
