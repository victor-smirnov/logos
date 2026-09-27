use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn main() {
    let v: Vec<i64> = vec![1, 2, 3];
    let b: Vec<i64> = v.into_iter().rev().collect();
    println!("{:?}", b);
    let w: Vec<i64> = vec![4, 5];
    let c: Vec<i64> = w.into_iter().map(|x: i64| -> i64 { return x * 2; }).collect();
    println!("{:?}", c);
}
