use std::ops::Index;
struct Grid { cells: Vec<i32> }
impl Index<usize> for Grid { type Output = i32; fn index(&self, p: usize) -> &i32 { &self.cells[p] } }
fn main() { let g = Grid { cells: vec![1, 2, 3, 4] }; let i: bool = true; println!("{}", g[i]); }
