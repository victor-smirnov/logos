struct Acc { hist: Vec<i64> }
impl Acc { fn consume(self) -> Vec<i64> { return self.hist; } }
struct W { inner: Box<Acc> }
fn main() { let b = Box::new(Acc { hist: vec![3] }); let w0 = b.consume(); println!("{:?}", w0); let w = W { inner: Box::new(Acc { hist: vec![4] }) }; let v = w.inner.consume(); println!("{:?}", v); }
