trait Shape { const SIDES: u32; }
struct Sq;
impl Shape for Sq { const SIDES: u32 = 4; }
fn make() -> impl Shape { return Sq; }
fn main() { let s = make(); let n: Sq = s; println!("{}", Sq::SIDES); }
