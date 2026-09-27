enum E { Bad(char, i64), Empty }
fn f(r: &Result<i64, E>) -> i64 { match r { Err(E::Bad(c, p)) if *c >= 'a' => *p, Err(_) => -1, Ok(v) => *v } }
fn main() { println!("{} {} {}", f(&Err(E::Bad('q', 3))), f(&Err(E::Bad('#', 4))), f(&Ok(9i64))); }
