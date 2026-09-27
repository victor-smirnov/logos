struct Buf<const N: usize> { data: [i64; N], len: i64 }
impl<const N: usize> Buf<N> {
    fn new() -> Buf<N> { Buf::<N> { data: [0; N], len: 0 } }
    fn view(&self) -> &[i64] { &self.data[0..self.len as usize] }
}
fn main() {
    let mut b: Buf<4> = Buf::new();
    b.data[0] = 5; b.data[1] = 6; b.len = 2;
    let v = b.view();
    println!("{} {}", v.len(), v[1]);
}
