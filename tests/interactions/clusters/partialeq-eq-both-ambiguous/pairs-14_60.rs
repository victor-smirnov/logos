#[derive(PartialEq, Eq)]
struct V { a: u32 }
fn main() { println!("{}", V { a: 1 } == V { a: 1 }); }
