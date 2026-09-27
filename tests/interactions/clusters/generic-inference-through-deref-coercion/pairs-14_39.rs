fn sum_by<T, F: Fn(&T) -> i64>(xs: &Vec<T>, f: F) -> i64 { let mut s = 0i64; for x in xs { s += f(x); } return s; }
fn app<F: Fn(&String) -> i64>(f: F) -> i64 { f(&String::from("xyz")) }
fn main() { let d: Vec<i64> = vec![3, 4]; println!("{}", sum_by(&d, |x| *x * 2)); println!("{}", app(|s| s.len() as i64)); }
