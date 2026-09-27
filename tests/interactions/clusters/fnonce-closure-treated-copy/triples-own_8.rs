use std::rc::Rc; use std::collections::HashMap; use std::cell::RefCell;
struct Holder<F: Fn(i64) -> i64> { f: F }
fn apply<F: Fn(i64) -> i64>(h: Holder<F>, x: i64) -> i64 { return (h.f)(x); }
fn run() {
    let b: Box<i64> = Box::new(7);
    let h = Holder { f: move |x: i64| -> i64 { return x * *b; } };
    println!("{}", apply(h, 3));
    println!("{}", apply(h, 4));
}
fn main() { run(); }
