struct Poly<const N: usize> { v: [i64; N] }
impl<const N: usize> Poly<N> { fn bump(&mut self) { for p in self.v.iter_mut() { *p += 100; } } fn bump2(&mut self) { self.v[0] += 5; } }
fn main() { let mut u = Poly::<2> { v: [1, 2] }; u.bump(); u.bump2(); println!("{} {}", u.v[0], u.v[1]); }
