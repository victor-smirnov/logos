struct W<J> { inner: J }
fn g<T>(_x: T) {}
fn main() { g(W { inner: vec![1i64, 2, 3] }); }
