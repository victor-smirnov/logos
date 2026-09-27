#[derive(Debug)]
enum T { Leaf(i64), Pair { a: i64, b: i64 }, Nil }
fn main() { println!("{:?} {:?} {:?}", T::Leaf(3), T::Pair { a: 1, b: 2 }, T::Nil); }
