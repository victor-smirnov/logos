const N: usize = 4;
struct Buf<const C: usize> { data: [i64; C] }
impl<const C: usize> Buf<C> { fn cap(&self) -> usize { C + self.data.len() * 0 } }
fn main() { let b = Buf::<N> { data: [0; N] }; let c: Buf<N> = Buf { data: [1; N] }; println!("{} {}", b.cap(), c.cap()); }
