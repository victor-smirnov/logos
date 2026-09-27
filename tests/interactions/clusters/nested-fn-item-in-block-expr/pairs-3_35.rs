fn main() { let r = { fn w() -> i64 { 77 } w() + 1 }; println!("{}", r); }
