fn f(k: i64) -> i64 { let v = { match k { 1 => 10, _ => 20 } }; return v; }
fn g(k: i64) -> i64 { let v = match k { 1 => { let t = k * 2; match t { 2 => 5, _ => 6 } } _ => 0 }; return v; }
fn main() { println!("{} {} {}", f(1), g(1), g(3)); }
