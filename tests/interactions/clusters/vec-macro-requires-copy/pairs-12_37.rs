trait Tag { fn tag(&self) -> i32; }
struct A(i32);
struct B { v: i32 }
impl Tag for A { fn tag(&self) -> i32 { return self.0; } }
impl Tag for B { fn tag(&self) -> i32 { return self.v * 2; } }
impl Drop for A { fn drop(&mut self) { println!("dropA {}", self.0); } }
impl Drop for B { fn drop(&mut self) { println!("dropB {}", self.v); } }
fn main() {
    let o: Option<Box<dyn Tag>> = Some(Box::new(B { v: 1 }));
    println!("o {}", o.is_some());
    let v: Vec<Box<dyn Tag>> = vec![Box::new(A(2))];
    println!("v {}", v.len());
}
