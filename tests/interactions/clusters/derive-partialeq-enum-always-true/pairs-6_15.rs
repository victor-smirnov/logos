#[derive(PartialEq)]
enum Dir { N, E }
fn main() { println!("{} {}", Dir::E == Dir::E, Dir::N == Dir::E); }
