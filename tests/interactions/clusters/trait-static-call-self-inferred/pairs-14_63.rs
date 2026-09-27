#[derive(Default)]
struct P { name: String, size: i64, flag: bool }
fn main() { let p = P { size: 3, ..Default::default() }; println!("{} {} {}", p.name.len(), p.size, p.flag); }
