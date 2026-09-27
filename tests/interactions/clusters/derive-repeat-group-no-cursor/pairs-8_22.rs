#[derive(Clone)]
enum E { A(i64), B }
fn main() { let e = E::A(3); let f = e.clone(); match f { E::A(x) => println!("{}", x), E::B => println!("b") } }
