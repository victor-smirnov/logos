struct D(i64, i64);
fn main() { let b = Box::new(D(3, 4)); let x = b.0; println!("{}", x); }
