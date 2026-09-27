fn main() {
    let v: Vec<i64> = vec![1, 2];
    let c = v.iter().map(|l| l * 2).collect::<Vec<_>>();
    println!("{:?}", c);
}
