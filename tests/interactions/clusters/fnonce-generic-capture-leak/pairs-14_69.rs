fn consume<F: FnOnce() -> String>(f: F) -> usize { f().len() }
fn main() { let name = String::from("closure"); let n = consume(move || name + "!"); println!("{}", n); }
