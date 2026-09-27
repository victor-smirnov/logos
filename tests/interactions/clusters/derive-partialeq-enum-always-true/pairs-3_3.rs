#[derive(PartialEq)]
enum E { A, B(i64), C(i64, i64) }
fn main() { println!("{} {} {} {}", E::B(3) == E::A, E::B(3) == E::B(4), E::C(1, 2) == E::C(1, 3), E::A == E::A); }
