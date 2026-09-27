fn pairs(a: Option<i64>, b: Option<i64>) -> i64 {
    if let Some(x) = a && let Some(y) = b && x < y { x + y } else { 0 }
}
fn main() { println!("{} {}", pairs(Some(1), Some(2)), pairs(Some(3), None)); }
