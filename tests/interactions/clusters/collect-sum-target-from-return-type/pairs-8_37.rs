fn f(v: Vec<i64>) -> Vec<i64> { return v.into_iter().map(|x| x * 2).collect(); }
fn main() { let v = f(vec![1i64, 2]); println!("{:?}", v); }
