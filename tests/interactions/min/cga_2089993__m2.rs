fn n<const K: usize>(xs: [i64; K]) -> i64 { xs.len() as i64 }
fn main() { let a: [i64; 3] = [1, 2, 3]; println!("{}", n(a)); }
