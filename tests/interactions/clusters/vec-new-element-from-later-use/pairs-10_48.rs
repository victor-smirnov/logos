fn f(acc: Vec<i64>) -> usize { acc.len() as usize }
fn g(acc: &mut Vec<i64>) { acc.push(1); }
fn main() { println!("{}", f(Vec::new())); g(&mut Vec::new()); }
