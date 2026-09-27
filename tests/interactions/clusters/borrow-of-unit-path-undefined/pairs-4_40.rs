use std::rc::Rc;
struct U;
impl U { fn v(&self) -> i64 { 3 } }
fn f(u: &U) -> i64 { u.v() }
fn main() {
    println!("{}", f(&U));
    let b = Box::new(String::from("bx")); let r = Rc::new(String::from("rc")); let bi = Box::new(5i64);
    println!("{} {} {}", b, r, bi);
}
