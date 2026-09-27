#[derive(Debug)]
struct C { n: i32 }
struct G { c: C }
impl Drop for G { fn drop(&mut self) { println!("g {:?}", self.c); } }
fn main() { let _g = G { c: C { n: 1 } }; }
