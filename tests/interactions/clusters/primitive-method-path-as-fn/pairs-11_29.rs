fn main() { let v = vec![3, 9, 2]; let m = v.iter().copied().fold(i32::MIN, i32::max); println!("{}", m); }
