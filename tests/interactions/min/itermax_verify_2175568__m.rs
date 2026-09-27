fn f(items: &Vec<u16>) -> Option<u16> { let mx = items.iter().max()?; Some(*mx) }
fn main() { let v: Vec<u16> = vec![4u16, 9, 2]; std::process::exit(match f(&v) { Some(x) => x as i32, None => 1 }) }
