use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
struct P { a: i64, b: i64 }
fn main() {
    let pair = |x: i64| -> (i64, i64) { return (x, x + 1); };
    let t = pair(5);
    println!("{} {}", t.0, t.1);
    let mkp = |x: i64| -> P { return P { a: x, b: x * 2 }; };
    let p = mkp(7);
    println!("{} {}", p.a, p.b);
}
