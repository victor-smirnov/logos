fn c<T: Clone>(x: &T) -> T { x.clone() }
fn main() { let s: &str = "ab"; let t = c(&s); println!("{}", t); }
