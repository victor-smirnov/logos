


fn chain(x: Option<i32>, y: Option<i32>) -> i32 {
    if let Some(a) = x && let Some(b) = y && a < b { 1 } else if let Some(_) = x { 2 } else { 3 }
}
fn main() {
    println!("{} {} {}", chain(Some(1), Some(2)), chain(Some(3), Some(2)), chain(None, Some(1)));
}
