#[derive(Clone, Copy, PartialEq, Eq)]
struct Pt { x: i32, y: i32 }
fn main() { let a = Pt { x: 1, y: 2 }; println!("{} {}", a == a, a != Pt { x: 0, y: 2 }); }
