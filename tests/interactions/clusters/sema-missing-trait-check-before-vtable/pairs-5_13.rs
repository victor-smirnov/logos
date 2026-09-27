use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
trait Item { fn weight(&self) -> i64; }
struct A { w: i64 }
struct N { w: i64 }
impl Item for A { fn weight(&self) -> i64 { return self.w; } }
fn main() {
    let mut v: Vec<Box<dyn Item>> = Vec::new();
    v.push(Box::new(A { w: 5 }));
    v.push(Box::new(N { w: 5 }));
    println!("{}", v.len());
}
