enum E { A(i64), C }
fn main() { let e = E::A(3); let x: i64 = loop { match e { E::A(n) => break n, E::C => break "c" } }; println!("{}", x); }
