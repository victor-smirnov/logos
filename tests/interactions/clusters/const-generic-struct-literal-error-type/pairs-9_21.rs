struct V<const N: usize> { c: [i64; N] }
impl<const N: usize> V<N> { fn unit() -> Self { V { c: [1; N] } } fn len(&self) -> usize { N } }
fn main() { let b: V<3> = V::unit(); println!("{} {}", b.len(), b.c[2]); }
