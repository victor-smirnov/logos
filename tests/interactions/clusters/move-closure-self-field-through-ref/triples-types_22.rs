struct C { items: Vec<i64> }
impl C {
    fn counter(&self) -> impl Fn() -> i64 + '_ { move || self.items.len() as i64 }
}
fn main() { let c = C { items: vec![1, 2] }; let k = c.counter(); println!("{} {}", k(), c.items.len()); }
