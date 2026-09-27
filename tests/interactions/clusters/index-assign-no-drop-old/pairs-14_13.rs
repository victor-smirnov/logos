use std::ops::{Index, IndexMut};
struct S { items: Vec<Box<i64>> }
impl Index<usize> for S { type Output = Box<i64>; fn index(&self, i: usize) -> &Box<i64> { &self.items[i] } }
impl IndexMut<usize> for S { fn index_mut(&mut self, i: usize) -> &mut Box<i64> { &mut self.items[i] } }
fn main() { let mut s = S { items: Vec::new() }; s.items.push(Box::new(1)); s[0] = Box::new(2); println!("{}", *s[0]); }
