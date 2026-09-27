fn f(s: &[i32]) -> i32 { match s { [] => 0, [x] => *x, [x, y] => x + y } }
fn main() { println!("{}", f(&[1])); }
