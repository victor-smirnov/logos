fn fill(out: &mut Vec<i64>) { out.push(1); }
fn main() { let mut v = Vec::new(); fill(&mut v); println!("{}", v.len()); }
