fn main() { let v = vec![3i64, 8]; match v.iter().max() { Some(&m) if m > 5 => println!("big {}", m), Some(_) => println!("small"), None => {} } }
