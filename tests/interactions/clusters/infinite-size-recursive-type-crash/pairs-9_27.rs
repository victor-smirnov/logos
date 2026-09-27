struct N { v: i64, next: Option<N> }
fn main() { let n = N { v: 1, next: None }; println!("{}", n.v); }
