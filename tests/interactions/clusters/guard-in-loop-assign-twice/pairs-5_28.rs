use std::collections::{HashMap, BTreeMap, BTreeSet, HashSet, VecDeque};
use std::rc::Rc;
enum Ev { Click(i64), Key(char) }
fn main() {
    let evs: Vec<Ev> = vec![Ev::Click(3), Ev::Key('a'), Ev::Click(5)];
    let mut t = 0i64;
    for e in evs.into_iter() { match e { Ev::Click(n) if n > 4 => { t += n; } _ => { t += 1; } } }
    println!("{}", t);
}
