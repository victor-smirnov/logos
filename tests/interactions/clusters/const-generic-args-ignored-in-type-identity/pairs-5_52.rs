use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
use std::ops::{Add, Sub, Mul, Neg, Index, IndexMut, AddAssign};
struct Mat<const N: usize> { d: [i64; N] }
fn add<const N: usize>(a: &Mat<N>, b: &Mat<N>) -> i64 { return a.d[0] + b.d[0]; }
fn main() { let a = Mat::<2> { d: [1, 2] }; let b = Mat::<3> { d: [1, 2, 3] }; println!("{}", add(&a, &b)); }
