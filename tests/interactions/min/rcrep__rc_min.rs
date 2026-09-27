use std::cell::RefCell;
fn main() {
    let c = RefCell::new(String::from("a"));
    let old = c.replace(String::from("b"));
    println!("{} {}", old, c.borrow().len());
}
