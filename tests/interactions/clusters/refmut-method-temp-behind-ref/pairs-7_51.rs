use std::cell::RefCell;
struct N { kids: Vec<i64> }
fn main() {
    let c = RefCell::new(N { kids: Vec::new() });
    { let mut g = c.borrow_mut(); g.kids.push(6); }
    println!("{}", c.borrow().kids.len());
}
