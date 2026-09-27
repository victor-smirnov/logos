fn main() { let v: Vec<i64> = vec![1, 2]; let s: i64 = v.iter().filter(|&x| x % 2 == 0).sum(); let w: Vec<i64> = v.iter().map(|&y| y + 1).filter(|&x| x > 2).collect(); println!("{} {:?}", s, w); }
