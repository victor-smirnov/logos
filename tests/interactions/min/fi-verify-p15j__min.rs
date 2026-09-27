fn main() { let a: Option<i32> = Some(5); match &a { None => println!("none"), Some(_) => println!("some") } }
