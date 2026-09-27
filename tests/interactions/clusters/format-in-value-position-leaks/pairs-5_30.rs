use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
fn main() {
    for q in 0..3i64 {
        let p = match q { 0 => format!("zero{}", q), _ => format!("x{}", q) };
        let r = if q == 1 { format!("one") } else { format!("n{}", q) };
        println!("{} {}", p, r);
    }
}
