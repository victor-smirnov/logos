struct Pair<A> { a: A, b: A }
fn main() { let p = Pair { a: 1i64, b: 2u8 }; println!("{}", p.a); }
