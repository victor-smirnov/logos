use std::rc::Rc;
struct A { bal: i64 }
fn main() { let mut r = Rc::new(A { bal: 1 }); let r2 = Rc::clone(&r); r.bal = 5; println!("{} {}", r.bal, r2.bal); }
