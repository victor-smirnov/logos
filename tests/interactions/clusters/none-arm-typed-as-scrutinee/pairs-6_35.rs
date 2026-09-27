fn main() { let a: Option<i32> = Some(3); let b: Option<i32> = match &a { Some(n) => Some(*n + 1), None => None }; println!("{:?}", b); }
