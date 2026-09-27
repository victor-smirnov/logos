trait S { fn nx(&mut self) -> Option<i64>; }
struct C { c: i64 }
impl S for C { fn nx(&mut self) -> Option<i64> { if self.c > 0 { self.c -= 1; return Some(self.c); } return None; } }
fn main() {
    let mut bx: Box<dyn S> = Box::new(C { c: 2 });
    let b: &mut Box<dyn S> = &mut bx;
    let a = b.nx();
    let c = b.nx();
    println!("{:?} {:?}", a, c);
}
