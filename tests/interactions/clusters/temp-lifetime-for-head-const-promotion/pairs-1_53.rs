fn main() { let a = 1i64; let b = 2i64; let mut s = 0i64; for v in [a, b].iter() { s += *v; } println!("{}", s); }
