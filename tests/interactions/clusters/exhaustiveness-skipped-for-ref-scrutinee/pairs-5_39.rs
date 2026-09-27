use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
enum Ev { Click(i64), Key(char), Scroll }
fn f(e: &Ev) -> i64 { return match e { Ev::Click(n) => *n, Ev::Key(_) => 1 }; }
fn main() { println!("{}", f(&Ev::Scroll)); }
