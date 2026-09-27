trait Tag { fn tag(&self) -> i32; }
struct A(i32);
impl Tag for A { fn tag(&self) -> i32 { return self.0; } }
fn g(x: &Box<dyn Tag>) -> i32 { return x.tag(); }
fn main() {
    let b: Box<dyn Tag> = Box::new(A(1));
    println!("fn {}", g(&b));
    let f = |x: &Box<dyn Tag>| -> i32 { return x.tag(); };
    println!("cl {}", f(&b));
}
