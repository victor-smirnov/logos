// the OTHER pinned short side: an INVARIANT carrier struct.
struct Inv<'a> { p: &'a mut &'a i32 }
fn needs<'a, 'b>(_x: &'a i32, _y: Inv<'b>) -> i32 where 'a: 'b { 0i32 }
fn caller<'p, 'q>(p: &'p i32, q: Inv<'q>) -> i32 { needs(p, q) }
fn main() { std::process::exit(0); }
