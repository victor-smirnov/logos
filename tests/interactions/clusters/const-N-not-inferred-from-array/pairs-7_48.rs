struct Fixed<const N: usize> { data: [i64; N] }
impl<const N: usize> Fixed<N> { fn cap(&self) -> usize { N } }
fn main() { let a = Fixed { data: [1, 2, 3] }; println!("{}", a.cap()); }
