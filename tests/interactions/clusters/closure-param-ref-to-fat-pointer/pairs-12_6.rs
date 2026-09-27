fn main() {
    let v: Vec<i64> = vec![1, 5, 2, 7];
    let a: Vec<i64> = v.windows(2).map(|w| w[0] * 10 + w[1]).collect();
    println!("{:?}", a);
    let mut b: Vec<i64> = Vec::new();
    for w in v.windows(2).filter(|w| { println!("see {} {}", w[0], w[1]); w[0] < w[1] }) { b.push(w[1]); }
    println!("{:?}", b);
    let c: Vec<i64> = v.chunks(2).filter(|c| c[0] < 3).map(|c| c[1]).collect();
    println!("{:?}", c);
}
