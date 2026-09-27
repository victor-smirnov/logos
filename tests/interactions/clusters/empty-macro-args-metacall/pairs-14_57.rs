fn f(o: Option<i64>) -> i64 { let v = match o { Some(x) => x, None => panic!() }; v * 2 }
fn main() { println!("{}", f(Some(4))); }
