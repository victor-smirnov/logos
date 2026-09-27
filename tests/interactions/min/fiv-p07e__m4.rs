trait Src { fn get(&self) -> i64; }
struct Seq { v: i64 }
impl Src for Seq { fn get(&self) -> i64 { self.v } }
fn lookup() -> Option<impl Src> { Some(Seq { v: 4 }) }
fn main() { match lookup() { Some(s) => println!("{}", s.get()), None => {} } }
