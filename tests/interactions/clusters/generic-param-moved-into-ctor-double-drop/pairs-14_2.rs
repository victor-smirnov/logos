fn bx<T>(t: T) -> Box<T> { let r = Box::new(t); return r; }
fn main() { let b = bx(String::from("de")); println!("{}", b.len()); }
