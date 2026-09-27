fn keep<F: FnOnce() -> String>(f: F) -> impl FnOnce() -> String { f }
fn main() { let s = String::from("a"); let g = keep(move || s); println!("{}", g()); println!("{}", g()); }
