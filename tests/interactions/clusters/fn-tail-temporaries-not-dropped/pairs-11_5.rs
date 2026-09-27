use std::cell::RefCell;
fn get(c: &RefCell<i32>) -> i32 { *c.borrow() }
fn main() { let c = RefCell::new(1); println!("{}", get(&c)); *c.borrow_mut() = 5; println!("{}", get(&c)); }
