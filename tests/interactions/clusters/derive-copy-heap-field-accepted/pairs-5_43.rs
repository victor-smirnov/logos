use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
#[derive(Clone, Copy)]
struct Rec { name: String, n: i64 }
fn main() { let a = Rec { name: String::from("a"), n: 1 }; let b = a; println!("{} {}", a.n, b.n); }
