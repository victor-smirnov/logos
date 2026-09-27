fn main() {
    let src: Vec<i64> = vec![1i64, 1, 2, 2, 3];
    let r: Vec<i64> = src.into_iter().map(|x| x + 1).collect();
    println!("{:?}", r);
}
