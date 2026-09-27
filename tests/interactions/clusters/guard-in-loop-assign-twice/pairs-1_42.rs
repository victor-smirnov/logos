fn main() { let v: Vec<i64> = vec![-1, 2]; for x in v.iter() { match x { n if *n < 0 => println!("neg {}", n), m => println!("{}", m) } } }
