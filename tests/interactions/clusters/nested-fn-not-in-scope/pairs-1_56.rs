fn main() { fn fact(n: i64) -> i64 { if n <= 1 { return 1; } return n * fact(n - 1); } println!("{}", fact(5)); }
