enum E<T> { A(T), B }
impl<T> E<T> { fn tag(&self) -> i32 { 1 } }
fn g<T>(w: &E<T>) -> i32 { w.tag() }
fn main() { let w = E::A(3); println!("{}", g(&w)); let _ = E::<i32>::B; if let E::A(_) = w {} }
