use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
use std::ops::{Add, Sub, Mul, Neg, Index, IndexMut, AddAssign};
const BASE: [i64; 3] = [1, 1, 2];
static TABLE: [i64; 2] = [7, 9];
fn main() { println!("{} {} {}", BASE[0], BASE[2], TABLE[1]); }
