struct Acc { total: i64, log: Vec<i64> }
impl Acc {
    fn consume<F: FnOnce(Vec<i64>) -> i64>(self, f: F) -> i64 { f(self.log) }
}
fn main() {
    let mut a = Acc { total: 0, log: Vec::new() };
    a.log.push(3); a.log.push(4);
    let n = a.consume(|v: Vec<i64>| { let mut s: i64 = 0; for x in v.iter() { s += *x; } s });
    println!("{}", n);
}
