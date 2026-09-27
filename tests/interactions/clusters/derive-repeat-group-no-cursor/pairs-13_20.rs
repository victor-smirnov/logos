#[derive(Clone)]
enum C { A(i32), B }
fn main() {
    let a = C::A(4);
    let b = a.clone();
    match (a, b) { (C::A(x), C::A(y)) => println!("{} {}", x, y), _ => println!("no") }
}
