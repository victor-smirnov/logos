trait Container { type Item; fn get(&self) -> Self::Item; }
struct Flags { a: bool }
impl Container for Flags { type Item = bool; fn get(&self) -> bool { self.a } }
fn f<C: Container<Item = i64>>(c: &C) -> i64 { c.get() }
fn main() { let x = Flags { a: true }; println!("{}", f(&x)); }
