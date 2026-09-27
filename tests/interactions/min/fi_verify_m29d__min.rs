use std::cmp::Ordering;
fn main() { let x = [1i64, 2]; let p: *const i64 = &x[0]; let q: *const i64 = &x[1]; let o = p.cmp(&q); println!("{}", o == Ordering::Less); }
