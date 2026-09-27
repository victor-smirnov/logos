use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn main() { let x = 5i64; let p: *const i64 = &x; let r: &i64 = &*p; println!("{}", r); }
