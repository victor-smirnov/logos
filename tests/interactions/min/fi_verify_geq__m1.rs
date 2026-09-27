struct W<T> { a: T }
impl<T: PartialEq> W<T> { fn ok(&self) -> bool { true } }
fn main() { let p = W { a: 3i64 }; if p.ok() { std::process::exit(5); } std::process::exit(0); }
