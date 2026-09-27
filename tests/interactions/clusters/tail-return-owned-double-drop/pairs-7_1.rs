struct D(i32);
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.0); } }
fn mk() -> D { let d = D(1); d }
fn mk2() -> D { let mut d = D(2); d.0 = 3; d }
fn mk3() -> String { let s = String::from("x"); s }
fn mk4() -> String { let mut s = String::from("x"); s.push('y'); s }
fn main() { let a = mk(); let b = mk2(); println!("{} {}", a.0, b.0); println!("{}", mk3()); println!("{}", mk4()); }
