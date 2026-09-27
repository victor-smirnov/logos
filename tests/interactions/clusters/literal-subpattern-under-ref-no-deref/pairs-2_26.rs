fn d(r: &(i64, i64)) -> i64 { match r { (x @ 1..=9, _) => *x, (x, y) => *x + *y } }
fn main() { let a = (7i64, 30i64); println!("{}", d(&a)); }
