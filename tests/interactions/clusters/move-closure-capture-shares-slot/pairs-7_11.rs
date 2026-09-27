use std::rc::Rc;
use std::cell::RefCell;
fn main() {
    let counter = Rc::new(RefCell::new(0));
    let inc = { let c = Rc::clone(&counter); move |k: i32| { *c.borrow_mut() += k; } };
    inc(5); inc(6);
    println!("{}", *counter.borrow());
}
