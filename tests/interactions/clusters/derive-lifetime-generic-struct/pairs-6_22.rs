#[derive(Debug)]
struct Wrap<'a> { p: &'a i32, k: u8 }
fn main() { let x = 5; let w = Wrap { p: &x, k: 1 }; println!("{} {}", *w.p, w.k); }
