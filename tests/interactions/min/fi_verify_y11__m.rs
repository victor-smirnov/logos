trait Speak { fn say(&self) -> i64; }
struct Loud { x: i64 }
impl Speak for Loud { fn say(&self) -> i64 { self.x } }
fn main() {
    let b: Box<dyn Speak> = Box::new(Loud { x: 7 });
    let bb: Box<Box<dyn Speak>> = Box::new(b);
    println!("{}", bb.say());
}
