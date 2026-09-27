fn f() -> Box<i64> { let b = Box::new(5i64); b }
fn g() -> String { let s = String::from("abc"); s }
fn main() { let b = f(); let s = g(); println!("{} {}", *b, s); }
