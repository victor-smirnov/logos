use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
use std::ops::{Add, Sub, Mul, Neg, Index, IndexMut, AddAssign};
fn depth<const D: usize>(n: usize) -> usize { if n > 20 { return 999; } if n >= D { return n; } return depth::<D>(n + 1); }
fn show<const D: usize>() -> usize { return D; }
fn main() { println!("{} {}", show::<7>(), depth::<7>(0)); }
