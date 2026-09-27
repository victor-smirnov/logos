#[derive(PartialEq, Debug)]
enum E { A(i32), B { x: i32 }, C }
fn main() {
    let a = E::A(1); let a2 = E::A(2); let b = E::B { x: 1 }; let c = E::C;
    println!("{} {} {} {} {}", a == b, a == a2, a == E::A(1), b == c, c == E::C);
    println!("{:?} {:?} {:?}", a, b, c);
}
