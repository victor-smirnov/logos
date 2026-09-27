fn total<const N: usize>(a: [i64; N]) -> i64 { let mut t: i64 = 0; for x in a.iter() { t += *x; } return t; }
fn n_of<const N: usize>(a: [i64; N]) -> usize { return N; }
fn len_of<const N: usize>(a: [i64; N]) -> usize { return a.len(); }
fn main() { let l = [2i64, 3, 5]; println!("{} {} {}", total(l), n_of(l), len_of(l)); }
