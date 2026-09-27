struct Ring<const N: usize> { d: [i64; N], head: usize }
impl<const N: usize> Ring<N> { fn new() -> Self { Ring { d: [0; N], head: 0 } } }
fn main() { let r: Ring<3> = Ring::new(); println!("{} {}", r.d.len(), r.head); }
