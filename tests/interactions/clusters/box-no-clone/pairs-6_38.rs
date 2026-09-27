fn main() { let b = Box::new(5); let c = b.clone(); let v: Vec<Box<i32>> = vec![Box::new(1)]; let w = v.clone(); println!("{} {} {}", *b, *c, *w[0]); }
