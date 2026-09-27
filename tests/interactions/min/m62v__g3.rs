trait Maker { type Out; }
struct StrMaker;
impl Maker for StrMaker { type Out = String; }
fn a3<M: Maker>(b: Box<M::Out>) -> M::Out { return *b; }
fn main() { println!("{}", a3::<StrMaker>(Box::new(String::from("p9")))); }
