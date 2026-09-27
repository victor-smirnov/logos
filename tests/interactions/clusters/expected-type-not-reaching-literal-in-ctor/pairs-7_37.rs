fn show<B: std::fmt::Display>(p: Result<B, String>) -> String { match p { Ok(b) => format!("{}", b), Err(e) => e } }
fn main() {
    println!("{}", show::<i64>(Ok(7)));
    println!("{}", show::<u8>(Ok(200)));
}
