
struct R { x: i64 }
impl R { fn get_mut(&self, i: i64) -> i64 { self.x + i } }
fn main() { let b = Box::new(R { x: 4 }); println!("{}", b.get_mut(1)); }
