fn s(xs: &[i64]) -> i64 { xs.iter().sum() }
fn main() {
    let v: Vec<i64> = vec![1, 2, 3, 4];
    println!("{} {} {}", s(&v[..]), s(&v[1..3]), s(&v[2..]));
}
