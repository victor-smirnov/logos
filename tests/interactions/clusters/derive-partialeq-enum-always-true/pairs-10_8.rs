#[derive(PartialEq)]
enum S { A(i64), B { w: i64 }, C }
fn main() { println!("{} {} {}", S::A(1) == S::A(1), S::A(1) == S::A(2), S::C == S::B { w: 1 }); }
