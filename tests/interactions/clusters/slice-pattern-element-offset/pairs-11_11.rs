fn main() { let a = [(1i32, 10i64), (2, 20), (3, 30)]; let v: &[(i32, i64)] = &a; match v { [_, rest @ ..] => println!("{} {} {}", rest.len(), rest[0].0, rest[1].1), [] => {} } }
