struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
struct W { a: D, b: i64 }

fn g(p: *mut i64) -> i64 { let x = D { v: 1, c: p }; let o = Some(D { v: 2, c: p }); match o { y @ Some(_) => { if y.is_some() { return 4; } return 3; } None => {} } return x.v; }
fn main() { let mut n: i64 = 0; let p = &mut n as *mut i64; let k = g(p); println!("k={} n={}", k, unsafe { *p }); }
