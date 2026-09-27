use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
#[derive(PartialEq)]
struct Rec { tags: Vec<i64>, n: i64 }
fn main() {
    let a: Vec<i64> = vec![1, 2];
    let b: Vec<i64> = vec![1, 2];
    println!("{}", a == b);
    println!("{}", Rec { tags: a.clone(), n: 1 } == Rec { tags: b.clone(), n: 1 });
}
