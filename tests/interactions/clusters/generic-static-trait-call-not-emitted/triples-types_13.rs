fn conv<T: Into<i64>>(x: T) -> i64 { let v: i64 = x.into(); v + 1 }
fn main() { println!("{}", conv(5i64)); let s: String = String::from("a").into(); println!("{}", s); }
