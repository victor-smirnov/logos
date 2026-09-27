#[derive(Clone, Copy)]
struct V { x: i64 }
impl PartialEq for V { fn eq(&self, o: &V) -> bool { self.x == o.x } }
impl Eq for V {}
fn main() { let a = V { x: 1 }; println!("{} {}", a != V { x: 2 }, vec![1, 2] == vec![1, 2]); }
