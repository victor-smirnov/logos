fn main() { let a = 5i64; let f = move || (a, 1i64); let r = f(); println!("{} {}", r.0, r.1); }
