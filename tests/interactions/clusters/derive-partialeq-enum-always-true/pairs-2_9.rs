#[derive(Clone, Copy, PartialEq)]
enum Color { Red, Green, Blue }
fn main() {
    let a = Color::Red; let b = Color::Blue;
    println!("{} {} {}", a == b, a == Color::Red, b == Color::Blue);
}
