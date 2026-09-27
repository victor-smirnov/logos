use std::collections::*;
enum Slot { A(i64), B }
fn main() { let s = Slot::A(3); match s { Slot::A(v) => println!("{}", v), Slot::B => println!("b") } }
