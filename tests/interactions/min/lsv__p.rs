struct S { x: i64 }
impl S {
    fn get<'a>(&'a self) -> &'a i64 { &self.x }
}
fn main() {
    let s = S { x: 7 };
    println!("{}", s.get());
}
