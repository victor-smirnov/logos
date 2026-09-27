use std::rc::Rc;
use std::cell::Cell;
fn main() {
    let shared = Rc::new(Cell::new(5));
    shared.set(7);
    println!("{}", shared.get());
}
