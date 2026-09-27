use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
struct D { id: i64 }
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.id); } }
fn mk(id: i64) -> Option<D> { return Some(D { id: id }); }
fn main() {
    if let None = mk(1) { println!("none"); } else { println!("else branch"); }
    println!("end");
}
