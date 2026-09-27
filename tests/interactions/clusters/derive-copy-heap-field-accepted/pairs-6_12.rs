#[derive(Clone, Copy)]
struct M { v: String }
fn main() { let a = M { v: String::from("x") }; let b = a; println!("{} {}", a.v, b.v); }
