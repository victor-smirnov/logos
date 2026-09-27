trait Counter { fn get(&self) -> i64; }
struct C { n: i64 }
impl Counter for C { fn get(&self) -> i64 { return self.n; } }
fn main() {
    let b = Box::new(C { n: 5 });
    println!("{}", b.get());
}
