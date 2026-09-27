fn main() {
    let mut v: Vec<(i64, i64)> = vec![(2, 1), (1, 5), (1, 2)];
    v.sort();
    println!("{:?} {}", v, (1, 2) < (1, 3));
    let mut w: Vec<i64> = vec![3, 1, 2];
    w.sort_by_key(|x| (x % 2, *x));
    println!("{:?}", w);
}
