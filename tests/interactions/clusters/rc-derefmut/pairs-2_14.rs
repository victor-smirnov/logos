use std::rc::Rc;
fn main() { let mut a = Rc::new(5i64); let b = a.clone(); *a += 1; println!("{} {}", *a, *b); }
