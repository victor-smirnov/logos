#[derive(Clone, Copy, PartialEq)]
struct M(i64, bool);
fn main() { let a = M(1, true); let b = M(1, false); println!("{} {}", a == b, a == a); }
