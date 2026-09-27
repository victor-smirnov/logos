struct B<const N: usize> { d: [i64; N], len: usize }
impl<const N: usize> B<N> { fn new() -> Self { B { d: [0; N], len: 0 } } }
fn main() { let b: B<3> = B::new(); println!("{}", b.d[2] + b.len as i64); }
