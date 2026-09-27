fn main() {
    let pages: Vec<i64> = vec![1, 2, 3];
    let p2: Vec<i64> = pages.into_iter().map(|p| p * 2).collect();
    println!("{:?}", p2);
}
