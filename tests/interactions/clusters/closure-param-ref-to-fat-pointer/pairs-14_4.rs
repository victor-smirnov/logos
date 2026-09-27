trait Op { fn apply(&self, x: i64) -> i64; }
struct AddN(i64);
impl Op for AddN { fn apply(&self, x: i64) -> i64 { x + self.0 } }
fn main() { let a = AddN(2); let b = AddN(5); let refs: Vec<&dyn Op> = vec![&a, &b]; let outs: Vec<i64> = refs.iter().map(|o| o.apply(1)).collect(); println!("{:?}", outs); }
