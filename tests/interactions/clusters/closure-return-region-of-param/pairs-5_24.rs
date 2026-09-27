use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn main() {
    let names: Vec<String> = vec![String::from("bob"), String::from("alice"), String::from("eve")];
    let longest = names.iter().fold(&names[0], |acc: &String, s: &String| -> &String { if s.len() > acc.len() { return s; } return acc; });
    println!("{}", longest);
}
