fn need<T: Clone>(_x: T) -> i64 { 1 }
fn main() { let a = 5; let r = &a; std::process::exit(need(r) as i32); }
