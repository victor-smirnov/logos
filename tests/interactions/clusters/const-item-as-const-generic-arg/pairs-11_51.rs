use std::ops::Add;
#[derive(Clone, Copy)]
struct V<const N: usize> { c: [i64; N] }
impl<const N: usize> Add for V<N> { type Output = V<N>; fn add(self, o: V<N>) -> V<N> { let mut r = self; for i in 0..N { r.c[i] += o.c[i]; } return r; } }
fn main() { let a = V::<2> { c: [1, 2] }; let b = a + a; println!("{} {}", b.c[0], b.c[1]); }
