fn main() {
    let v: Vec<i64> = vec![1, 25, 30];
    let a: Vec<_> = v.iter().map(|x| x * 2).collect();
    println!("{:?}", a);
}
