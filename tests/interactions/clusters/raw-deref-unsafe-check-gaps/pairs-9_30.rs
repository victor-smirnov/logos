struct A { t: i64 }
impl A { fn bump(&mut self) { self.t += 1; } }
fn main() { let a = A { t: 3 }; let p: *const A = &a; unsafe { (*p).bump(); } println!("{}", a.t); }
