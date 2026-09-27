struct Buf<const N: usize> { d: [i64; N] }
impl<const N: usize> Buf<N> {
    fn new(seed: i64) -> Self { let d = [seed; N]; Buf { d } }
}
fn main() { let b: Buf<3> = Buf::new(2); println!("{}", b.d[2]); }
