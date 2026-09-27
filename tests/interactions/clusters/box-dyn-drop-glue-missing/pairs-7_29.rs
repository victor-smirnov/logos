trait S { fn n(&self) -> i64; }
struct E { v: Vec<i64> }
impl S for E { fn n(&self) -> i64 { self.v.len() as i64 } }
fn main() {
    let mut vs: Vec<Box<dyn S>> = Vec::new();
    vs.push(Box::new(E { v: vec![1, 2, 3] }));
    println!("{}", vs[0].n());
}
