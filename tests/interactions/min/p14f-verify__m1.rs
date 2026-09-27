fn f(v: &[Option<i64>]) -> i64 { match v { [Some(a)] => *a, _ => 0 } }
fn main() { println!("{}", f(&[Some(1)])); }
