trait Parser { type Out; fn parse(&self) -> Option<Self::Out>; }
struct D;
impl Parser for D { type Out = i64; fn parse(&self) -> Option<i64> { Some(20) } }
fn run<P: Parser<Out = i64>>(p: &P) -> Option<i64> { let x = p.parse()?; Some(x + 1) }
fn main() { let d = D; println!("{:?}", run(&d)); }
