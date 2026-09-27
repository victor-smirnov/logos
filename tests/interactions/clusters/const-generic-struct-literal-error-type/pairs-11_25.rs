struct B<const N: usize> { d: [u8; N] }
impl<const N: usize> B<N> { fn new() -> Self { B { d: [0; N] } } fn n(&self) -> usize { N } }
fn main() { let b: B<3> = B::new(); println!("{}", b.n()); }
