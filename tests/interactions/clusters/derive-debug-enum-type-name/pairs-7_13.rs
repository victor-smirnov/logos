#[derive(Clone, Copy, PartialEq, Debug)]
enum Lvl { Low, Mid(i32), High { v: i32 } }
fn pick(flag: bool) -> impl std::fmt::Debug { if flag { return Lvl::Mid(3); } return Lvl::High { v: 9 }; }
fn main() {
    println!("{:?} {:?} {:?}", Lvl::Low, Lvl::Mid(3), Lvl::High { v: 9 });
    println!("{:?}", pick(true));
}
