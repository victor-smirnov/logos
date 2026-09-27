fn dup<T: Clone>(t: &T) -> (T, T) { (t.clone(), t.clone()) }
fn main() { let x = 5i64; let (a, b) = dup(&&x); println!("{} {}", *a + *b, a); let s = "hi"; let (c, d) = dup(&s); println!("{}{}", c, d); }
