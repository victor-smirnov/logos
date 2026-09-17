fn first(x: [i64; 2]) -> i64 { x[0] }
fn main() { let v = first([5, 6]); std::process::exit((v as i32) - 5); }
