fn apply_all<T, F: Fn(T) -> T>(xs: Vec<T>, f: F) -> Vec<T> { xs.into_iter().map(f).collect() }
fn main() { let v = apply_all(vec![1i32, 2, 3], |x| x * x + 1); println!("{:?}", v); }
