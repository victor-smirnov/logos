struct Counter<T> { items: Vec<T>, hits: i64 }
impl<T: Clone + PartialEq> Counter<T> {
    fn count(&self, x: &T) -> i64 { let mut n = 0i64; for y in self.items.iter() { if y == x { n += 1; } } return n; }
}
fn main() {
    let mut c: Counter<i64> = Counter { items: Vec::new(), hits: 0 };
    c.items.push(3);
    println!("{}", c.count(&3));
}
