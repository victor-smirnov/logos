use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
use std::cell::{Cell, RefCell, UnsafeCell};
use std::ops::{Add, Sub, Mul, Neg, Index, IndexMut, AddAssign};
fn f(v: &Vec<i64>) -> i64 { return v.iter().sum(); }
fn g(v: &Vec<i64>) -> Vec<i64> { return v.iter().map(|x| x * 2).collect(); }
fn main() { let v: Vec<i64> = vec![1, 2, 3]; println!("{} {:?}", f(&v), g(&v)); }
