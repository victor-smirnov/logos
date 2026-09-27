#[derive(Clone, Copy)] struct Q { a: i64 }
fn mq() -> Q { Q { a: 7 } }
fn main() {
    let f = |q: Q| q.a;
    let r = f(mq());
    if r != 7 { panic!("wrong"); }
}
