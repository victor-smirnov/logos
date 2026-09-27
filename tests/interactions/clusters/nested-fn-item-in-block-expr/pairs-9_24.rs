fn main() { let v = 3i64; let msg = { fn inc(n: i64) -> i64 { n + 1 } inc(v) }; println!("{}", msg); }
