fn sum(s: &[i64]) -> i64 { let mut t = 0i64; for x in s { t += *x; } return t; }
fn main() {
    let v: Vec<i64> = vec![1, 2, 3, 4];
    let a = &v[1..];
    let b = &v[..2];
    println!("{} {} {}", sum(a), sum(b), sum(&v[1..3]));
}
