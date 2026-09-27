fn mk() -> String { let s = String::from("de"); s }
fn main() { let b = mk(); std::process::exit(b.len() as i32); }
