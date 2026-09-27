use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
struct D { id: i64, s: String }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
fn main() {
    let d = D { id: 1, s: String::from("heap") };
    let D { id, .. } = d;
    println!("{} {}", id, d.id);
    let e = D { id: 2, s: String::from("heap2") };
    if let D { id: 2, .. } = e { println!("matched"); }
    println!("end");
}
