use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
#[derive(PartialEq, Eq)]
struct Pt { x: i64, y: i64 }
fn main() { println!("{} {}", Pt { x: 1, y: 2 } == Pt { x: 1, y: 2 }, Pt { x: 1, y: 2 } != Pt { x: 1, y: 3 }); }
