struct Bad { v: i64, next: Option<Bad> }
fn main() { let b = Bad { v: 1, next: None }; println!("{}", b.v); }
