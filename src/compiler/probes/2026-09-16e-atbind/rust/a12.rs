struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
struct W { a: D, b: i64 }

fn g(p: *mut i64) -> i64 { let mut i = 0; let mut k = 0; while i < 2 { let w = W { a: D { v: 2, c: p }, b: 3 }; match w { y @ W { .. } => { k = y.b; } } i += 1; } return k; }
fn main() { let mut n: i64 = 0; let p = &mut n as *mut i64; let k = g(p); println!("k={} n={}", k, unsafe { *p }); }
