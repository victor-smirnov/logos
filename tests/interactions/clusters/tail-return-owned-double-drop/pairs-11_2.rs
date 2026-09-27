struct D(i32);
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.0); } }
fn pw() -> D { let d = D(1); d }
fn main() { let v = pw(); println!("got {}", v.0); }
