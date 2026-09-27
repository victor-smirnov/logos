#[derive(Debug)]
enum E { A(i32), B }
fn main() {
    println!("{:?} {:?}", E::A(5), E::B);
}
