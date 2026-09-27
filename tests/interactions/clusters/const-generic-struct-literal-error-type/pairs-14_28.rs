struct Buf<const N: usize> { items: [i64; N], len: usize }
impl<const N: usize> Buf<N> { fn new() -> Self { Buf { items: [0; N], len: 0 } } }
fn main() { let b: Buf<2> = Buf::new(); println!("{} {}", b.len, b.items[1]); }
