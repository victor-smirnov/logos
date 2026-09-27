fn main() {
    let xs: [(i64, i64); 2] = [(1, 10), (2, 20)];
    if let Some(&(k, v)) = xs.iter().next() { println!("{} {}", k, v); }
    let o: Option<&i64> = xs.iter().map(|p| &p.0).last();
    if let Some(&m) = o { println!("{}", m); }
}
