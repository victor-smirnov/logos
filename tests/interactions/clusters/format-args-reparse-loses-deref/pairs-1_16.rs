trait Counter { fn get(&self) -> i64; }
struct ByOne { n: i64 }
impl Counter for ByOne { fn get(&self) -> i64 { self.n } }
fn look(c: &dyn Counter) -> i64 { c.get() }
fn main() {
    let b: Box<dyn Counter> = Box::new(ByOne { n: 10 });
    println!("{}", look(&*b));
}
