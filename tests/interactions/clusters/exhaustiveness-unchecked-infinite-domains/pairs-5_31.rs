use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn f(xs: &[i64]) -> i64 { return match xs { [] => 0, [x] => *x, [a, b] => a + b }; }
fn main() { println!("{}", f(&[1, 2, 3])); }
