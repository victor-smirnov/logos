use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]

struct Pt { x: i64, y: i64 }
fn main() {
    let pts: Vec<Pt> = vec![Pt { x: 3, y: 1 }, Pt { x: 1, y: 5 }];
    let mx = pts.iter().max().unwrap();
    println!("{} {}", mx.x, mx.y);
}
