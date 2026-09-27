fn s(xs: &[i64]) -> i64 { let t: i64 = xs.iter().sum(); t }
fn main() {
    let v: Vec<i64> = vec![1, 2, 3, 4];
    let a = &v[1..3];
    println!("{} {}", s(a), s(&v[..]));
}
