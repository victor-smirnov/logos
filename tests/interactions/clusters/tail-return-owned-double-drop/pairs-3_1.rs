fn g(v: Vec<i64>) -> Vec<i64> { v }
fn h(s: String) -> String { s }
fn k() -> String { let s = String::from("abc"); s }
fn main() { let v = g(vec![1i64, 2]); println!("{:?}", v); let s = h(String::from("q")); println!("{}", s); println!("{}", k()); }
