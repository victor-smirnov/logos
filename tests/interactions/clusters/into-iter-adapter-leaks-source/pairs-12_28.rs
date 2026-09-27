use std::collections::BTreeSet;
fn main() {
    let mut s: BTreeSet<i64> = BTreeSet::new();
    s.insert(3); s.insert(1);
    let v: Vec<i64> = s.into_iter().collect();
    println!("{:?}", v);
}
