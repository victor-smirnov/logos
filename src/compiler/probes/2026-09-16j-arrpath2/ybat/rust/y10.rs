struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
#[allow(dead_code)] struct W { a: D, b: D }
#[allow(dead_code)] struct V { a: D, n: i64 }
#[allow(dead_code)] struct P { t: (D, D) }
fn g(p: *mut i64) -> i64 {
    let arr: [P; 1] = [P { t: (D { v: 1, c: p }, D { v: 2, c: p }) }];
    match arr { [P { t: (x, _) }] => { return x.v; } }
}
fn main() { let mut n: i64 = 0; let p: *mut i64 = &mut n; let k = g(p); println!("k={} n={}", k, unsafe { n }); }
