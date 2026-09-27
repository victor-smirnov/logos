fn main() { let mut c = 0; let mut f = |x: i64| if x > 1 { c += 1; }; f(2); f(0); f(3); println!("{}", c); }
