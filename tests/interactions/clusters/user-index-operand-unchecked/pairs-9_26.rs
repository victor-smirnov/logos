use std::ops::Index;
struct C { v: Vec<i64> }
impl Index<usize> for C { type Output = i64; fn index(&self, k: usize) -> &i64 { &self.v[k] } }
fn main() { let c = C { v: vec![1i64, 2] }; println!("{}", c["a"]); }
