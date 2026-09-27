trait Tag { fn tag(&self) -> i32; }
struct A(i32);
struct B { v: i32 }
impl Tag for A { fn tag(&self) -> i32 { return self.0; } }
impl Tag for B { fn tag(&self) -> i32 { return self.v * 2; } }
impl Drop for A { fn drop(&mut self) { println!("dropA {}", self.0); } }
impl Drop for B { fn drop(&mut self) { println!("dropB {}", self.v); } }
struct Hold<T> { x: Option<T> }
impl<T> Hold<T> { fn put(&mut self, v: T) { self.x = Some(v); } }
fn main() {
    let mut h: Hold<Box<dyn Tag>> = Hold { x: None };
    h.put(Box::new(A(2)));
    let mut v: Vec<Box<dyn Tag>> = Vec::new();
    v.push(Box::new(B { v: 3 }));
    println!("end");
}
