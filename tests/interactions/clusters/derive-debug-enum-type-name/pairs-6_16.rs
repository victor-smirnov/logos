#[derive(Debug)]
enum E { Empty, Bad(u8), Big { v: i64 } }
fn main() { println!("{:?} {:?} {:?}", E::Empty, E::Bad(3), E::Big { v: 9 }); let r: Result<i32, E> = Err(E::Bad(1)); println!("{:?}", r); }
