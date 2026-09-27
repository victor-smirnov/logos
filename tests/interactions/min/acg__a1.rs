struct Buf<const M: usize> {}
impl<const M: usize> Buf<M> { const LEN: usize = M; }
fn main() { println!("{}", Buf::<4>::LEN); }
