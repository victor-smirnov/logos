fn sq(x: i64) -> i64 { x * x }
fn main() {
    let v = [1i64, 2, 3];
    let a: Vec<i64> = v.iter().map(|&x| sq(x)).collect();
    let b: i64 = v.iter().map(|&x| x * 2).sum();
    println!("{:?} {}", a, b);
}
