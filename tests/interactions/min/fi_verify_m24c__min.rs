fn fe(v: &Vec<i64>) -> i64 { match v.iter().position(|x| *x == 4) { Some(i) => i as i64, None => -1 } }
fn main() { let v: Vec<i64> = vec![1, 4]; std::process::exit(fe(&v) as i32); }
