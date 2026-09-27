fn main() { let v: Vec<Option<i64>> = vec![Some(5), Some(500)]; for r in v.iter() { match r { Some(n) if *n > 100 => { println!("big {}", n); } _ => { println!("other"); } } } }
