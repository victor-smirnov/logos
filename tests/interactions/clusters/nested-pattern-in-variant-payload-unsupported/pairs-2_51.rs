enum E { Bad(char, i64) }
fn f(e: &E) -> i64 { match e { E::Bad(c @ 'a'..='z', p) => *p, E::Bad(_, p) => -*p } }
fn main() { println!("{} {}", f(&E::Bad('q', 3)), f(&E::Bad('#', 4))); }
