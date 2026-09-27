fn sum_a(v: Vec<i64>) -> i64 { v.into_iter().sum() }
fn sum_b(v: Vec<i64>) -> i64 { let s: i64 = v.into_iter().sum(); s }
fn main() {
    let mut v: Vec<i64> = Vec::new(); v.push(3); v.push(4);
    println!("{}", sum_b(v.clone()));
    println!("{}", sum_a(v));
}
