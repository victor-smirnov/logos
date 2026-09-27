#[derive(Debug)]
#[allow(dead_code)]
enum Cmd { Push(i64), Pop, At { x: i64 } }
fn main() { println!("{:?} {:?} {:?}", Cmd::Push(3), Cmd::Pop, Cmd::At { x: 1 }); }
