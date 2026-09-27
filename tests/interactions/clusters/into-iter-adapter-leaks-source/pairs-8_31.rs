fn main() {
    let v: Vec<i64> = vec![4, 5, 6];
    let s: i64 = v.into_iter().map(|x| x * 2).sum();
    println!("{}", s);
}
