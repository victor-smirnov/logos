#[derive(Clone)]
struct C { name: String, o: Option<i64> }
fn main() { let a = C { name: String::from("x"), o: Some(3) }; let b = a.clone(); println!("{} {:?} {}", b.name, b.o, a.name); }
