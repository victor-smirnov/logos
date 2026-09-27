struct S { name: String }
impl Clone for S { fn clone(&self) -> S { *self } }
impl Copy for S {}
fn main() { let s = S { name: String::from("a") }; let t = s; println!("{} {}", s.name, t.name); }
