#[derive(PartialEq, PartialOrd)]
struct V2 { x: i64, y: i64 }
fn main() {
    let a = V2 { x: 1, y: 2 }; let b = V2 { x: 1, y: 10 };
    println!("{} {} {}", a < b, a == b, a.partial_cmp(&b) == Some(std::cmp::Ordering::Less));
}
