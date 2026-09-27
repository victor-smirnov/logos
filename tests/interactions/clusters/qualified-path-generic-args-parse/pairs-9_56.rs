struct V<const N: usize>;
impl<const N: usize> V<N> { fn n(&self) -> usize { N } }
fn main() { let v = V::<4>; println!("{}", v.n()); }
