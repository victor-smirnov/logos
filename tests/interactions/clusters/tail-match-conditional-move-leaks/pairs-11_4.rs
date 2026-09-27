struct D(i32);
impl Drop for D { fn drop(&mut self) { println!("drop {}", self.0); } }
fn pick(f: u8, a: D, b: D) -> D { match f { 0 => a, _ => b } }
fn pick2(f: u8, a: D, b: D) -> D { let r = match f { 0 => a, _ => b }; return r; }
fn main() { let r = pick(1, D(1), D(2)); println!("got {}", r.0); let s = pick2(1, D(3), D(4)); println!("got {}", s.0); }
