trait Item { fn weight(&self) -> i64; }
struct N { w: i64 }
fn main() {
    let b: Box<dyn Item> = Box::new(N { w: 5 });
}
