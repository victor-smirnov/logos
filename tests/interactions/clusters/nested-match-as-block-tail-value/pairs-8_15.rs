fn f(k: i32, o: Option<i32>) -> i32 { if k < 5 { match o { Some(v) => v + k, None => 0 } } else { 1 } }
fn main() { let o: Option<i32> = Some(2); println!("{} {} {}", f(5, o), f(2, o), f(9, o)); }
