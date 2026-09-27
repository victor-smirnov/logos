#[derive(PartialEq)]
struct T2(i32, u8);
fn main() { println!("{} {}", T2(1, 2) == T2(1, 2), T2(1, 2) == T2(1, 3)); }
