#[allow(unused_imports)]
use std::collections::HashMap;
#[allow(dead_code)]
enum Slot { Empty, Full(i64) }
fn main() { let s = Slot::Full(4); match s { Slot::Full(v) => println!("{}", v), Slot::Empty => println!("e") } }
