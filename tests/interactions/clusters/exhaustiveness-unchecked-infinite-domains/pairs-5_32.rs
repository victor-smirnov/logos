use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn f(x: u8) -> i64 { return match x { 0..=100 => 1, 101..=254 => 2 }; }
fn main() { println!("{}", f(255)); }
