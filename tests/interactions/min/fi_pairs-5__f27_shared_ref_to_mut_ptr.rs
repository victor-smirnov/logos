use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn main() {
    let x = 5i64;
    let p: *mut i64 = &x;
    unsafe { *p = 6; }
    println!("{}", x);
}
