trait Act { fn act(&mut self) -> i64; }
struct C { n: i64 }
impl Act for C { fn act(&mut self) -> i64 { self.n += 1; return self.n; } }
fn main() {
    let mut b: Box<dyn Act> = Box::new(C { n: 0 });
    let a: &mut Box<dyn Act> = &mut b;
    a.act();
    std::process::exit(a.act() as i32);
}
