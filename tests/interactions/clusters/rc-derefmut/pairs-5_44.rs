use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
struct P { x: i64 }
fn main() {
    let mut r = Rc::new(P { x: 5 });
    let r2 = Rc::clone(&r);
    r.x = 6;
    let mut q = Rc::new(1i64);
    let q2 = q.clone();
    *q = 9;
    println!("{} {} {}", r.x, r2.x, *q2);
}
