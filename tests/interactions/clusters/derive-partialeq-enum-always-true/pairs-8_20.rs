#[derive(PartialEq)]
#[allow(dead_code)]
enum Cell { Empty, Wall, Coin(u8) }
fn main() {
    println!("{} {} {} {}", Cell::Wall != Cell::Empty, Cell::Wall == Cell::Empty, Cell::Coin(1) == Cell::Coin(2), Cell::Coin(3) == Cell::Coin(3));
}
