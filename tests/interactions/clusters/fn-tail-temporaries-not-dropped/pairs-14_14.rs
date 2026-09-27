use std::cell::RefCell;
fn peek(c: &RefCell<Option<i64>>) -> i64 { match &*c.borrow() { Some(v) => *v, None => 0 } }
fn main() { let c = RefCell::new(Some(5i64)); println!("{}", peek(&c)); *c.borrow_mut() = Some(7); println!("{}", peek(&c)); }
