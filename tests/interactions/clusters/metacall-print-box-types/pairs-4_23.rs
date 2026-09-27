enum E { A(i64), N(Box<E>) }
fn main() {
    let e = E::N(Box::new(E::A(4)));
    if let E::N(_) = e { print!("n "); }
    println!();
}
