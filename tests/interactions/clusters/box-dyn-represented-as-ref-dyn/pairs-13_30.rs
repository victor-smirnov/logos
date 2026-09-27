trait Problem { fn msg(&self) -> i32; }
struct Neg;
impl Problem for Neg { fn msg(&self) -> i32 { return 4; } }
impl From<Neg> for Box<dyn Problem> { fn from(n: Neg) -> Self { return Box::new(n); } }
fn main() {
    let b: Box<dyn Problem> = Box::from(Neg);
    println!("{}", b.msg());
}
