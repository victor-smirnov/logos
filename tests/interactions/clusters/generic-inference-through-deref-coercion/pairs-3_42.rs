fn count<S>(xs: &[S]) -> i64 { xs.len() as i64 }
fn main() {
    let mut v: Vec<i64> = Vec::new(); v.push(1); v.push(2);
    println!("{}", count(&v));
}
