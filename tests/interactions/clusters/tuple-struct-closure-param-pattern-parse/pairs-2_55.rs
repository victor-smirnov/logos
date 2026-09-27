struct Id(i64);
fn main() { let v: Vec<Id> = vec![Id(1), Id(2)]; let s: i64 = v.into_iter().map(|Id(k)| k).sum(); println!("{}", s); }
