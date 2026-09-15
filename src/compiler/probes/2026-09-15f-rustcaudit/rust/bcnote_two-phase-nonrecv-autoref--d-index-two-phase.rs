use std::ops::{Index, IndexMut};
struct I { v: i64 }
impl Index<i64> for I { type Output = i64; fn index(self: &I, i: i64) -> &i64 { return &self.v; } }
impl IndexMut<i64> for I { fn index_mut(self: &mut I, i: i64) -> &mut i64 { return &mut self.v; } }
fn coerce_index_op() {
    let mut i: I = I { v: 10i64 };
    i[i[3i64]] = 4i64;
    i[3i64] = i[4i64];
    i[i[3i64]] = i[4i64];
}
fn main() { coerce_index_op(); }
