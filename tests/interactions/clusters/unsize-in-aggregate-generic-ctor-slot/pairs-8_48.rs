



trait Op { fn apply(&self, x: i64) -> i64; }
struct AddN(i64);
impl Op for AddN { fn apply(&self, x: i64) -> i64 { x + self.0 } }
fn main() {
    let v: Vec<(i64, Box<dyn Op>)> = vec![(0, Box::new(AddN(5))), (1, Box::new(AddN(7)))];
    for (i, o) in v.iter() { println!("{} {}", i, o.apply(2)); }
}
