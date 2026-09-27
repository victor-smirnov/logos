fn dup<T: Clone>(t: &T) -> T { t.clone() }
fn main() { let s: &str = "hi"; let t = dup(&s); println!("{}", t); }
