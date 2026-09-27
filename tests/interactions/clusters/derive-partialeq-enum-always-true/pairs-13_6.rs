#[derive(PartialEq)]
enum C { A(i32), B }
fn main() {
    println!("{} {} {}", C::A(1) == C::A(1), C::A(1) == C::A(2), C::B == C::A(1));
}
