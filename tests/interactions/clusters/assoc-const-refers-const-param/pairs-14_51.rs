struct Buf<const M: usize> { d: [i64; M] }
impl<const M: usize> Buf<M> { const LEN: usize = M; }
fn main() { let n = Buf::<4>::LEN; println!("{}", n); }
