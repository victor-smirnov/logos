fn main() { let f = |x: i64| -> (i64, i64) { return (x, x + 1); }; let p: (i64, i64) = f(5); std::process::exit((p.0 + p.1) as i32); }
