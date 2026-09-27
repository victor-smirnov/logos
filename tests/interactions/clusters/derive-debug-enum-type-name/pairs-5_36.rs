use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
#[derive(Debug)]
enum Color { Red, Green }
#[derive(Debug)]
enum Sh { Dot(i64), Empty }
fn main() {
    println!("{:?} {:?}", Color::Red, Color::Green);
    println!("{:?} {:?}", Sh::Dot(3), Sh::Empty);
}
