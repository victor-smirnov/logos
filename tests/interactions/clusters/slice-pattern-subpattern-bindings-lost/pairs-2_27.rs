fn f(v: &[Option<i64>]) -> Option<i64> { match v { [Some(a)] => Some(*a), _ => None } }
fn main() { println!("{:?}", f(&[Some(1)])); }
