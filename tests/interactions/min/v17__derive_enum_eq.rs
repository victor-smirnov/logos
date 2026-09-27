#[derive(PartialEq)]
enum Dir { N, E }
fn main() { if Dir::N == Dir::E { std::process::exit(1); } }
