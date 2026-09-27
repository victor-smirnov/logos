struct Ring<const N: usize> { items: [i64; N] }
impl<const N: usize> Ring<N> {
    fn total(&self) -> i64 { let mut t: i64 = 0; for d in self.items.iter() { t += *d; } t }
    fn total2(&self) -> i64 { let mut t: i64 = 0; for i in 0..N { t += self.items[i]; } t }
    fn len(&self) -> i64 { self.items.len() as i64 }
}
fn sum<const N: usize>(a: &[i64; N]) -> i64 { let mut t: i64 = 0; for d in a.iter() { t += *d; } t }
fn main() { let r = Ring::<3> { items: [1, 2, 3] }; println!("{} {} {} {}", r.total(), r.total2(), r.len(), sum(&r.items)); }
