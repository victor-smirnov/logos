struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 1, c: p }, D { v: 2, c: p }];
    match arr { [ref r, y] => { return r.v + y.v; } }
}
fn main() { let mut n: i64 = 0; let p: *mut i64 = &mut n; let k = g(p); println!("k={} n={}", k, unsafe { n }); }
