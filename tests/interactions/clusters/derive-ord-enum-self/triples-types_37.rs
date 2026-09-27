#[derive(PartialEq, Eq, PartialOrd, Ord)]
enum Pri { Low, Mid(i64) }
fn main() { println!("{} {}", Pri::Low < Pri::Mid(1), Pri::Mid(2) > Pri::Mid(1)); }
