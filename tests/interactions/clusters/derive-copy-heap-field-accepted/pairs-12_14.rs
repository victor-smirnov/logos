#[derive(Clone, Copy)]
struct Pt { x: i32, s: String }
fn main() { let p = Pt { x: 1, s: String::from("hello") }; let q = p; println!("{} {}", p.s, q.s); }
