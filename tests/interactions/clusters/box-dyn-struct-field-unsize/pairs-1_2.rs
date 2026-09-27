trait Buf { fn total(&self) -> i64; }
struct Max { m: i64 }
impl Buf for Max { fn total(&self) -> i64 { self.m } }
struct Wrap { inner: Box<dyn Buf> }
fn main() {
    let w = Wrap { inner: Box::new(Max { m: 7 }) };
    println!("{}", w.inner.total());
}
