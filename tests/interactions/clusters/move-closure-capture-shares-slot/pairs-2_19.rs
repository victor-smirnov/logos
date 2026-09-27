trait Op { fn apply(&self, x: i64) -> i64; }
struct AddN { n: i64 }
impl Op for AddN { fn apply(&self, x: i64) -> i64 { x + self.n } }
fn compose(ops: Vec<Box<dyn Op>>) -> Box<dyn Fn(i64) -> i64> { Box::new(move |x: i64| { let mut v = x; for o in ops.iter() { v = o.apply(v); } v }) }
fn main() {
    let mut ops: Vec<Box<dyn Op>> = Vec::new();
    ops.push(Box::new(AddN { n: 3 })); ops.push(Box::new(AddN { n: 4 }));
    let f = compose(ops);
    println!("{}", f(1));
}
