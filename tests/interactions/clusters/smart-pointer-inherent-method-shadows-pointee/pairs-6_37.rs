use std::rc::Rc;
use std::cell::{Cell, RefCell};
#[derive(Clone)]
struct Shared { data: Rc<RefCell<Vec<i32>>>, hits: Rc<Cell<u32>>, own: String }
#[derive(Clone, Copy, PartialEq, Debug, Default)]
struct Pt { x: i32, y: i32 }
fn main() {
    let s = Shared { data: Rc::new(RefCell::new(vec![1])), hits: Rc::new(Cell::new(0)), own: String::from("o") };
    let mut s2 = s.clone();
    s2.data.borrow_mut().push(2);
    s2.hits.set(s2.hits.get() + 5);
    s2.own.push_str("x");
    *s.data.borrow_mut() = vec![9, 9, 9];
    println!("{} {} {} {} {}", s.data.borrow().len(), s2.data.borrow().len(), Rc::strong_count(&s.data), s.hits.get(), s.own);
    let cell = RefCell::new(Pt::default());
    cell.borrow_mut().x = 4;
    let snapshot = *cell.borrow();
    cell.borrow_mut().y = 8;
    println!("{:?} {:?} {}", snapshot, *cell.borrow(), snapshot == *cell.borrow());
    let rc_pt = Rc::new(Pt { x: 1, y: 2 });
    let copied: Pt = *rc_pt;
    let b = Box::new(copied);
    let back = *b;
    println!("{:?} {} {}", back, back == *rc_pt, Rc::strong_count(&rc_pt));
    let c = Cell::new(Pt { x: 0, y: 0 });
    c.set(Pt { x: c.get().x + 3, ..c.get() });
    println!("{:?}", c.get());
}
