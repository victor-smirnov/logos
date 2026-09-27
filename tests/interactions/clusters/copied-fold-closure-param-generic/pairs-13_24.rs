fn main() {
    let v: Vec<i64> = vec![3, 1, 4];
    let m = v.iter().copied().fold(0i64, |x, y| x + y);
    let n: i64 = v.iter().copied().map(|y| y + 1).sum();
    println!("{} {}", m, n);
}
