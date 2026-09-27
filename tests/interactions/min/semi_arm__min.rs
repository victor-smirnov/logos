fn f() -> i64 { 5 }
fn main() { let t = { f(); }; println!("{}", t + 1); }
