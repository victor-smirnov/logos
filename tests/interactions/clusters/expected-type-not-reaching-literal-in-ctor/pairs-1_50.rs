fn fo(o: Option<i64>) -> i64 { o.unwrap_or(0) }
fn main() { let f = |o: Option<i64>| o.unwrap_or(0); println!("{} {}", fo(Some(1)), f(Some(2))); }
