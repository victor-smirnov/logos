struct Sq { s: i64 }
fn main() { let v = vec![Sq { s: 2 }, Sq { s: 3 }]; println!("{}", v[0].s + v[1].s); }
