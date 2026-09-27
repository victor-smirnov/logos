use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn main() {
    let ps = [(1i64, 2i64), (3, 4)];
    println!("{:?}", ps);
    let arrs = [[1i64, 2], [3, 4]];
    println!("{:?}", arrs);
    let s: i64 = arrs[1][0] + arrs[1][1] + ps[1].0 + ps[1].1;
    println!("{}", s);
}
