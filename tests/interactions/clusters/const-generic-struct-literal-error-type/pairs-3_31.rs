struct Buf<const N: usize> { data: [i64; N], len: i64 }
impl<const N: usize> Buf<N> {
    fn new() -> Buf<N> { Buf { data: [0; N], len: 0 } }
}
fn main() { let b: Buf<4> = Buf::new(); println!("{} {}", b.len, b.data[3]); }
