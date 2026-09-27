#[derive(Clone, Copy)]
struct S { name: String }
fn main() { let s = S { name: String::from("a") }; let t = s; println!("{} {}", s.name, t.name); }
