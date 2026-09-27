use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
trait Visit { fn visit(&mut self, v: i64); }
struct S { t: i64 }
impl Visit for S { fn visit(&mut self, v: i64) { self.t += v; } }
fn go(vis: &mut impl Visit) { vis.visit(3); }
fn show(x: &impl Visit) -> i64 { return 1; }
fn main() { let mut s = S { t: 0 }; go(&mut s); println!("{} {}", s.t, show(&s)); }
