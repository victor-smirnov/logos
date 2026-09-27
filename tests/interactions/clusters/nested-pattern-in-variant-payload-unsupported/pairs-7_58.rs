fn main() {
    let v: Vec<i64> = vec![1, 2];
    let mut it = v.iter();
    if let Some(&n) = it.next() { println!("{}", n); }
    let o: Option<(i64, i64)> = Some((1, 2));
    if let Some((a, 2)) = o { println!("{}", a); }
}
