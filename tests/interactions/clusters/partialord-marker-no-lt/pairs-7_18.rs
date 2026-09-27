#[derive(Clone, Copy, PartialEq, PartialOrd)]
enum Lvl { Low, Mid(i32), High { v: i32 } }
#[derive(Clone, Copy, PartialEq, PartialOrd)]
enum C { A, B }
#[derive(Clone, Copy, PartialEq, PartialOrd)]
struct S { a: i32, b: i32 }
fn main() {
    println!("{}", C::A < C::B);
    println!("{}", S { a: 1, b: 5 } < S { a: 1, b: 7 });
    println!("{}", Lvl::Low < Lvl::Mid(0));
    println!("{}", Lvl::Mid(5) < Lvl::Mid(2));
    println!("{}", Lvl::High { v: 1 } > Lvl::Mid(100));
}
