struct B<const N: usize> { len: i64 }
impl<const N: usize> B<N> { fn new() -> Self { B { len: 7 } } }
fn main() { let b: B<3> = B::new(); println!("{}", b.len); }
