use std::rc::Rc;
use std::cell::RefCell;
struct P { a: i64 }
struct T(i64, i64);
fn main() {
    let s = Rc::new(RefCell::new(P { a: 1 }));
    s.borrow_mut().a += 5;
    let t = Rc::new(RefCell::new(T(1, 2)));
    t.borrow_mut().0 += 5;
    let b = Box::new(T(3, 4));
    let r = Rc::new(T(5, 6));
    println!("{} {} {} {}", s.borrow().a, t.borrow().0, b.1, r.0);
}
