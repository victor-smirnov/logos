fn main() {
    let mut g: Vec<String> = Vec::new(); g.push(String::from("a"));
    let keys: Vec<String> = g.into_iter().map(|s| s).collect();
    println!("{}", keys.len());
}
