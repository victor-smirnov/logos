fn dbl(x: i64) -> i64 { x * 2 }
fn main() {
    let v: Vec<i64> = vec![1, 2, 3];
    let w: Vec<i64> = v.iter().map(|&x| dbl(x)).collect();
    println!("{:?}", w);
}
