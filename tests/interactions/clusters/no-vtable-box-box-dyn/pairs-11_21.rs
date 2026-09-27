trait Speak { fn say(&self) -> String; }
struct Loud(String);
impl Speak for Loud { fn say(&self) -> String { format!("{}!", self.0) } }
fn main() {
    let b: Box<dyn Speak> = Box::new(Loud(String::from("a")));
    let bb: Box<Box<dyn Speak>> = Box::new(b);
    println!("{}", bb.say());
}
