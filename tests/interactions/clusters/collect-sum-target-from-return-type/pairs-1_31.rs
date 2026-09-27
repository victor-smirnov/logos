fn f(a: &[i32]) -> Vec<i32> { return a.iter().map(|x| *x + 1).collect(); }
fn main() { println!("{:?}", f(&[1, 2])); }
