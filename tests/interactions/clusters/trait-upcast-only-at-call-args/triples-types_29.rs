trait Named { fn name(&self) -> i64; }
trait Animal: Named { fn legs(&self) -> i64; }
struct D {}
impl Named for D { fn name(&self) -> i64 { 1 } }
impl Animal for D { fn legs(&self) -> i64 { 4 } }
fn via_super(a: &dyn Animal) -> i64 { let n: &dyn Named = a; n.name() }
fn main() { println!("{}", via_super(&D {})); }
