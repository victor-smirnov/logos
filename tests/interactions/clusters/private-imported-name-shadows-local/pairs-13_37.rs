use std::collections::HashMap;
enum Slot<T> { Empty, Full(T) }
struct Bag { a: Slot<i64> }
fn main() {
    let _m: HashMap<i32, i32> = HashMap::new();
    let b = Bag { a: Slot::Full(4) };
    match b.a { Slot::Full(v) => println!("{}", v), Slot::Empty => println!("e") }
}
