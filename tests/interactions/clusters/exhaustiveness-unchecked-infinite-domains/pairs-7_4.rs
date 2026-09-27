fn f(s: &str) -> i32 { match s { "a" => 1, "b" => 2 } }
fn main() { println!("{}", f("zz")); }
