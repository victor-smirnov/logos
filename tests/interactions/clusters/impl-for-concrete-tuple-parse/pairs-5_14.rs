use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
trait Metric { fn m(&self) -> i64; }
impl Metric for (i64, i64) { fn m(&self) -> i64 { return self.0 * 10 + self.1; } }
fn main() { let p = (1i64, 2i64); println!("{}", p.m()); }
