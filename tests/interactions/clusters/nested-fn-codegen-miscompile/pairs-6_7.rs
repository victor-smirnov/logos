trait Speak { fn say(&self) -> i32; }
struct A(i32);
impl Speak for A { fn say(&self) -> i32 { self.0 } }
fn main() {
    let s = A(5);
    fn helper(x: &dyn Speak) -> i32 { x.say() + 1000 }
    println!("{}", helper(&s));
}
