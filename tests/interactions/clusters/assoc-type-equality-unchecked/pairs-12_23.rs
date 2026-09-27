trait Shape { const SIDES: u32; type Unit; fn unit(&self) -> Self::Unit; }
struct Sq;
impl Shape for Sq { const SIDES: u32 = 4; type Unit = String; fn unit(&self) -> String { return String::from("a"); } }
fn make() -> impl Shape<Unit = i64> { return Sq; }
fn main() { println!("{}", make().unit()); }
