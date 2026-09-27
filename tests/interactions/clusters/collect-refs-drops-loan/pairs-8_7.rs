fn mk<'a>() -> Vec<&'a i64> { let v: Vec<i64> = vec![11, 22]; let out: Vec<&'a i64> = v.iter().collect(); return out; }
fn main() { let r = mk(); let w: Vec<i64> = vec![99, 98, 97, 96]; println!("{} {} {}", *r[0], *r[1], w.len()); }
