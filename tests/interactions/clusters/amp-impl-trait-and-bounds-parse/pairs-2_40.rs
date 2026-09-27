trait Animal { fn legs(&self) -> i64; }
struct D;
impl Animal for D { fn legs(&self) -> i64 { 4 } }
fn intro_ref(a: &impl Animal) -> i64 { a.legs() }
fn main() { println!("{}", intro_ref(&D)); }
