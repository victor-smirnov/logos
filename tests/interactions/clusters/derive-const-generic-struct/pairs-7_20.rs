#[derive(Clone, Copy)]
struct Poly<const N: usize> { v: [i32; N] }
static TRI: Poly<3> = Poly::<3> { v: [1, 2, 3] };
fn main() { println!("{}", TRI.v[2]); }
