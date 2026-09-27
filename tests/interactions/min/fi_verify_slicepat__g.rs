fn head(xs: &[i64]) -> &i64 { match xs { [a, ..] => a, _ => &xs[0] } }
fn main() { let v = [5i64, 6, 7]; std::process::exit(*head(&v) as i32); }
