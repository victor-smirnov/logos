struct G<T, const N: usize> { c: [T; N] }
impl<T: Copy, const N: usize> G<T, N> { fn n(&self) -> usize { N } }
fn main() { let g = G { c: [1i32, 2, 3] }; println!("{}", g.n()); }
