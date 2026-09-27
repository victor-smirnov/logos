struct R { v: Vec<i64> }
impl R { fn get(&self, i: usize) -> i64 { self.v[i] } }
fn main() { let b = Box::new(R { v: vec![4, 5] }); println!("{}", b.get(1)); }
