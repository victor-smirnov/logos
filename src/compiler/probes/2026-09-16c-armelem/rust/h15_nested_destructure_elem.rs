struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
struct W { a: D, b: D }
fn g(p: *mut i64) -> i64 {
    let arr: [W; 1] = [W { a: D { v: 1, c: p }, b: D { v: 2, c: p } }];
    match arr { [W { a: x, b: _ }] => { return x.v; } }
}
fn main() { let mut n: i64 = 0; let p: *mut i64 = &mut n; let k = g(p); println!("k={} n={}", k, unsafe { n }); }
