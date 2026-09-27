fn main() { let v: Vec<Option<i64>> = vec![Some(12), None, Some(30)]; for p in &v { println!("{:?}", p); } for p in &v { match p { Some(n) => println!("s {}", n), None => println!("none") } } }
