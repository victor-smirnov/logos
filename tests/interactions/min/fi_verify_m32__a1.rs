fn main() {
    let r: Vec<bool> = vec![1i64, -2, 3].into_iter().map(|y| y > 0).collect();
    println!("{:?}", r);
}
