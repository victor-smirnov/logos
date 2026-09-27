trait Counter { fn bump(&mut self) -> i64; }
struct ByOne { n: i64 }
impl Counter for ByOne { fn bump(&mut self) -> i64 { self.n += 1; self.n } }
fn drive(c: &mut dyn Counter) -> i64 { c.bump() }
fn main() {
    let mut b: Box<dyn Counter> = Box::new(ByOne { n: 10 });
    let rb = &mut b;
    let x = drive(&mut **rb);
    println!("{}", x);
}
