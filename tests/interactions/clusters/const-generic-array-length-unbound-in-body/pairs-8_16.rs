fn total<const K: usize>(xs: [i64; K]) -> i64 { let mut s = 0i64; for x in xs.iter() { s += *x; } return s + xs.len() as i64 * 100; }
fn total_idx<const K: usize>(xs: [i64; K]) -> i64 { let mut s = 0i64; let mut i = 0; while i < K { s += xs[i]; i += 1; } return s; }
fn main() { let a: [i64; 3] = [1, 2, 3]; println!("{} {}", total(a), total_idx(a)); }
