use std::collections::HashSet;
fn main() {
    let mut s: HashSet<char> = HashSet::new();
    s.insert('a');
    println!("{}", s.len());
}
