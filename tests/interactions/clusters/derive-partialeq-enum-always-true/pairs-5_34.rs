use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
#[derive(PartialEq)]
enum Color { Red, Green, Blue }
#[derive(PartialEq)]
enum Sh { Dot(i64), Empty }
fn main() {
    println!("{} {}", Color::Green == Color::Red, Color::Blue != Color::Red);
    println!("{} {}", Sh::Dot(1) == Sh::Empty, Sh::Dot(1) == Sh::Dot(2));
}
