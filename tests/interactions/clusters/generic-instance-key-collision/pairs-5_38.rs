use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
enum Json { Num(i64), Str(String) }
fn main() {
    let mut out: Vec<Option<i64>> = Vec::new();
    out.push(Some(2)); out.push(None);
    for o in out.iter() { if let Some(k) = o { println!("k {}", k); } }
    let mut opts: Vec<Option<Box<Json>>> = Vec::new();
    opts.push(Some(Box::new(Json::Num(4)))); opts.push(None); opts.push(Some(Box::new(Json::Str(String::from("x")))));
    let mut n = 0i64;
    for o in opts.iter() { if let Some(b) = o { if let Json::Num(k) = **b { n += k; } else { n += 100; } } }
    println!("{}", n);
}
