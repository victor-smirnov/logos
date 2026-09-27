use std::rc::Rc;
fn main() {
    let mut r = Rc::new(5i64);
    let r2 = Rc::clone(&r);
    let m: &mut i64 = &mut *r;
    *m = 42;
    println!("{} {}", *r, *r2);
}
