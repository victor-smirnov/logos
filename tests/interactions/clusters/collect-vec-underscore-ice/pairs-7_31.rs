fn main() {
    let w: Vec<i64> = vec![1, 2, 3];
    let d = w.iter().map(|x| x * 2).collect::<Vec<_>>();
    println!("{:?}", d);
}
