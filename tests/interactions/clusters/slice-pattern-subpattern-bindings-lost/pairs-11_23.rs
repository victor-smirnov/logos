fn f(v: &[(i32, i32)]) -> i32 { match v { [(a, _), ..] => *a, [] => 0 } }
fn main() { println!("{}", f(&[(2, 3)])); }
