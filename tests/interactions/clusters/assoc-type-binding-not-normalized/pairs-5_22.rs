use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
trait Shape { type Unit; fn sides(&self) -> Vec<Self::Unit>; }
struct Sq { s: i64 }
impl Shape for Sq { type Unit = i64; fn sides(&self) -> Vec<i64> { let v: Vec<i64> = vec![self.s, self.s + 1]; return v; } }
fn longest<S: Shape<Unit = i64>>(s: &S) -> i64 { let mut m = 0i64; for x in s.sides().iter() { if *x > m { m = *x; } } return m; }
fn main() { println!("{}", longest(&Sq { s: 4 })); }
