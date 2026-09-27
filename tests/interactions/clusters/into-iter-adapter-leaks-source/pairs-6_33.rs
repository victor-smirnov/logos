fn main() { let items: Vec<i32> = vec![1, 2, 3]; let big: Vec<i32> = items.into_iter().map(|i| i + 1).collect(); println!("{}", big.len()); }
