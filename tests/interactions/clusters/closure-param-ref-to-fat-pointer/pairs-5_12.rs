use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
trait Item { fn weight(&self) -> i64; }
struct A { w: i64 }
impl Item for A { fn weight(&self) -> i64 { return self.w; } }
fn g(x: &Box<dyn Item>) -> i64 { return x.weight(); }
fn main() {
    let b: Box<dyn Item> = Box::new(A { w: 5 });
    println!("{}", g(&b));
    let c = |x: &Box<dyn Item>| -> i64 { return x.weight(); };
    println!("{}", c(&b));
}
