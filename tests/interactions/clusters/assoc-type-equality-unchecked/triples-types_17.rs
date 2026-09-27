trait Container { type Item; fn first(&self) -> Self::Item; }
struct S {}
impl Container for S { type Item = String; fn first(&self) -> String { String::new() } }
fn need<C: Container<Item = i64>>(c: &C) -> i64 { 0 }
fn main() { let s = S {}; println!("{}", need(&s)); }
