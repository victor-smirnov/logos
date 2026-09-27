fn main() {
    let v: Vec<i64> = vec![5, 6];
    let a: Vec<(i64, i64)> = v.iter().map(|e| (*e, 1i64)).collect();
    println!("{} {} {}", a[0].0, a[0].1, a[1].0);
    let w: Vec<(i64, i64)> = vec![(1, 2), (3, 4)];
    println!("{:?}", w);
}
