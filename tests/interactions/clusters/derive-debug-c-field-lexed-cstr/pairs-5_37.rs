use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
#[derive(Debug)]
enum Color { Red, Green }
#[derive(Debug)]
struct W { c: Color, n: i64 }
fn main() { println!("{:?}", W { c: Color::Green, n: 2 }); }
