#[derive(Clone, Copy)]
struct Poly<const N: usize> { x: i32, id: i32 }
static TRI: Poly<3> = Poly::<3> { x: 3, id: 1 };
fn main() { println!("{} {}", TRI.x, TRI.id); }
