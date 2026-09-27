fn main() { let mut i = 0i64; while i < 2 { match i { n if n > 5 => {} _ => {} } i += 1; } println!("{}", i); }
