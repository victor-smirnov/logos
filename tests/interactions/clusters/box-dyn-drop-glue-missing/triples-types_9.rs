trait T { fn g(&self) -> i64; }
struct S { v: i64 }
impl T for S { fn g(&self) -> i64 { self.v } }
impl Drop for S { fn drop(&mut self) { println!("drop {}", self.v); } }
fn main() {
    { let mut cs: Vec<Box<dyn T>> = Vec::new(); cs.push(Box::new(S { v: 1 })); println!("{}", cs[0].g()); }
    println!("mid");
    { let b: Box<dyn T> = Box::new(S { v: 3 }); drop(b); println!("after drop"); }
    { let b: Box<S> = Box::new(S { v: 4 }); drop(b); println!("after drop4"); }
    println!("end");
}
