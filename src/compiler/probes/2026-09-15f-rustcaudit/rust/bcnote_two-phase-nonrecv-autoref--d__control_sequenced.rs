use std::ops::{Index, IndexMut};
struct I { v: i64 }
impl Index<i64> for I { type Output = i64; fn index(self: &I, i: i64) -> &i64 { return &self.v; } }
impl IndexMut<i64> for I { fn index_mut(self: &mut I, i: i64) -> &mut i64 { return &mut self.v; } }
fn main() {
    let mut i: I = I { v: 10i64 };
    let k = i[3i64];
    i[k] = 4i64;
    std::process::exit(i.v as i32);
}
