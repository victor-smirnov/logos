use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
use std::ops::{Add, Sub, Mul, Neg, Index, IndexMut, AddAssign};
fn sum_arr<const N: usize>(a: [i64; N], i: usize) -> i64 { if i >= N { return 0; } return a[i] + sum_arr(a, i + 1); }
fn main() { println!("{}", sum_arr([1i64, 2, 3, 4], 0)); }
