use std::ops::Index;
struct G { c: Vec<i32> }
impl Index<usize> for G { type Output = i32; fn index(&self, p: usize) -> &i32 { &self.c[p] } }
fn main() { let g = G { c: vec![1, 2] }; println!("{}", g[true]); }
