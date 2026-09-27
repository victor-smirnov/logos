use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn main() {
    let data: [i64; 3] = [4, -2, 7];
    let last2: Vec<&i64> = data.iter().rev().take(2).collect();
    println!("{:?}", last2);
}
