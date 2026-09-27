fn main() {
    let mut pairs: Vec<(String, i64)> = Vec::new();
    pairs.push((String::from("a"), 1)); pairs.push((String::from("b"), 2));
    pairs.reverse();
    println!("{} {}", pairs[0].0, pairs.len());
}
