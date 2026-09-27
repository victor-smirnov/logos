trait Speak { fn say(&self) -> i32; }
struct Loud(i32);
impl Speak for Loud { fn say(&self) -> i32 { self.0 } }
fn talk(s: &dyn Speak) -> i32 { s.say() }
fn main() {
    let b: Box<dyn Speak> = Box::new(Loud(4));
    println!("{}", talk(&*b));
}
