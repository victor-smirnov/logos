struct Buf<const C: usize> { data: [i64; C], len: i64 }
impl<const C: usize> Buf<C> {
    fn new() -> Self { Buf { data: [0; C], len: 0 } }
    fn cap(&self) -> usize { C + self.data.len() * 0 + self.len as usize }
}
fn main() { let b = Buf::<4>::new(); let s: Buf<2> = Buf::new(); println!("{} {}", b.cap(), s.cap()); }
