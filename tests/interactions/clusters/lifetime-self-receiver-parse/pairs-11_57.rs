struct S { x: i32 }
impl S { fn get<'a>(&'a self) -> &'a i32 { &self.x } }
fn main() { let s = S { x: 4 }; println!("{}", s.get()); }
