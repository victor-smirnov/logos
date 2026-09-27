use std::rc::Rc;



fn main() {
    let mut r = Rc::new(vec![1i64]);
    let r2 = Rc::clone(&r);
    let first: &i64 = &r2[0];
    for i in 0..100 { (&mut *r).push(i); }
    println!("{}", *first);
}
