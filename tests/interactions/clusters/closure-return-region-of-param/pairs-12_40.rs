fn main() {
    let v: Vec<(String, i64)> = vec![(String::from("b"), 2)];
    let a: Vec<&str> = v.iter().map(|(n, _)| n.as_str()).collect();
    let b: Vec<&String> = v.iter().map(|t| &t.0).collect();
    println!("{} {}", a.len(), b.len());
}
