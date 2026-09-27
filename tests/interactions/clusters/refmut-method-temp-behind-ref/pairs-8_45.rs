use std::cell::RefCell;
struct N { val: i64, kids: Vec<i64> }
fn main() {
    let c = RefCell::new(N { val: 1, kids: vec![] });
    c.borrow_mut().kids.push(5);
    let mut m = c.borrow_mut(); m.kids.push(6); m.val += 1; drop(m);
    println!("{} {}", c.borrow().kids.len(), c.borrow().val);
}
