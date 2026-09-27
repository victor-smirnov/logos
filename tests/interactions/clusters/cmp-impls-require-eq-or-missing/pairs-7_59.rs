#[derive(PartialEq, Clone, Copy)]
enum Sh { A(i32), B { x: i32 } }
fn main() {
    let o: Option<Sh> = Some(Sh::B { x: 2 });
    println!("{}", o == Some(Sh::B { x: 2 }));
    println!("{}", o == Some(Sh::A(2)));
    println!("{}", o != None);
}
