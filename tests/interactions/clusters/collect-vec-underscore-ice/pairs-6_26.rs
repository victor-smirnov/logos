fn parse(s: &str) -> Option<i64> { s.parse::<i64>().ok() }
fn main() { let parsed = ["12", "x", "30"].iter().map(|s| parse(*s)).collect::<Vec<_>>(); println!("{}", parsed.len()); }
