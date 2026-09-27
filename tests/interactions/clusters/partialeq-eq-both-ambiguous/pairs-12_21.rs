struct V { a: i32 }
impl PartialEq for V { fn eq(&self, o: &V) -> bool { return self.a == o.a; } }
impl Eq for V {}
fn main() { let x = V { a: 1 }; let y = V { a: 1 }; println!("{}", x == y); }
