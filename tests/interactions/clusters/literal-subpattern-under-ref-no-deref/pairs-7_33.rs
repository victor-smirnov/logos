enum Tok { Num(i64), Sym(char) }
fn d(t: &Tok) -> i64 { match t { Tok::Sym(c @ ('+' | '-')) => 1, _ => -1 } }
fn main() { println!("{} {}", d(&Tok::Sym('+')), d(&Tok::Num(30))); }
