fn f(o: Option<&i64>) -> i32 { match o { Some(7) => 1, _ => 2 } }
fn main() { let a = 7i64; std::process::exit(f(Some(&a))) }
