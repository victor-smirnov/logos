struct Ring<const N: usize> { buf: [i64; N] }
impl<const N: usize> Ring<N> { fn new() -> Self { Self { buf: [7; N] } } }
fn main() { let r: Ring<3> = Ring::new(); println!("{}", r.buf[2]); }
