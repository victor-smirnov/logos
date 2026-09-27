use std::collections::HashMap;
fn main() {
    let mut m: HashMap<i64, i64> = HashMap::new();
    m.insert(1, 5);
    for (k, v) in m.iter_mut() { *v += *k; }
    println!("{}", m[&1]);
}
