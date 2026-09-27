fn head(xs: &[i64]) -> Option<&i64> { match xs { [a, ..] => Some(a), [] => None } }
fn main() { let v = [5i64, 6, 7]; println!("{:?}", head(&v)); }
