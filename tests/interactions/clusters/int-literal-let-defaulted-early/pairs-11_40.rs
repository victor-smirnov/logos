fn main() { let xs = [1i64, 2, 3]; let r = xs.iter().fold(0, |acc, x| acc + x); println!("{}", r); }
