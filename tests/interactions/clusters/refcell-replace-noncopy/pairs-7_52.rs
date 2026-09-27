use std::cell::RefCell;
struct N { id: i32 }
impl Drop for N { fn drop(&mut self) { println!("drop {}", self.id); } }
fn main() {
    let c = RefCell::new(N { id: 1 });
    let old = c.replace(N { id: 2 });
    println!("{} {}", old.id, c.borrow().id);
}
