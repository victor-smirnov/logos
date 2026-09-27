trait T2 { fn total(&self) -> i64; }
struct Buf<const N: usize> { data: [i64; N] }
impl<const N: usize> T2 for Buf<N> { fn total(&self) -> i64 { self.data[0] + N as i64 } }
fn make<const N: usize>(fill: i64) -> impl T2 { Buf::<N> { data: [fill; N] } }
fn show(x: impl T2) -> i64 { x.total() * 10 }
fn main() { println!("{}", show(make::<2>(5))); }
