fn main() { let v = [1, 2, 3]; let w: Vec<_> = v.iter().map(|x| x * 2).collect(); println!("{}", w[2]); }
