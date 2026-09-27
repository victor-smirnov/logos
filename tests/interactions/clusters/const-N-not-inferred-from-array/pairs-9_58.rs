struct V<const N: usize> { c: [i64; N] }
impl<const N: usize> V<N> { fn n(&self) -> usize { N } }
fn main() { let a = V { c: [1, 2, 3] }; println!("{}", a.n()); }
