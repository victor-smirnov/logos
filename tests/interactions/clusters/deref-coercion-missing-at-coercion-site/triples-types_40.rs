fn len(s: &str) -> usize { s.len() }
fn take(x: &i64) -> i64 { *x }
fn main() { let xs = ["ab", "cde"]; let mut t = 0; for x in xs.iter() { t += len(x); } let y = 5i64; let r = &y; let rr = &r; println!("{} {}", t, take(rr)); }
