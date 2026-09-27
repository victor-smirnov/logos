enum L { R, G(i64) }
fn f(x: &L) -> i64 { match x { L::R => 1 } }
fn main() { println!("{}", f(&L::G(-1))); }
