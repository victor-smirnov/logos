fn cnt(t: &[(i64, i64)]) -> i64 { if let [first, rest @ ..] = t { let (k, _xs) = first; return *k + cnt(rest); } return 0; }
fn main() { let t: [(i64, i64); 3] = [(7, 1), (8, 1), (9, 1)]; println!("{}", cnt(&t)); }
