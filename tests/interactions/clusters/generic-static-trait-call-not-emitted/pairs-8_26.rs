

trait T { const K: i64 = 7; fn id(&self) -> i64; }
struct A {}
impl T for A { fn id(&self) -> i64 { 1 } }
fn k_of<X: T>(x: &X) -> i64 { X::K + x.id() }
fn main() { let a = A {}; println!("{}", k_of(&a)); }
