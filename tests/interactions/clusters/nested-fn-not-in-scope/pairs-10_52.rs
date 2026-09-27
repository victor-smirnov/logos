fn main() { fn inner(d: i32) -> i32 { if d == 0 { 0 } else { 1 + inner(d - 1) } } println!("{}", inner(4)); }
