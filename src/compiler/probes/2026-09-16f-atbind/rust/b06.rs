struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn rd(p: *mut i64) -> i64 { unsafe { *p } }
struct W { a: D, b: i64 }
fn g(p: *mut i64) -> i64 {
    let x = D { v: 1, c: p };
    let w = W { a: D { v: 2, c: p }, b: 3 };
    let mut k: i64 = 0;
    match w { y @ W { .. } => { k = y.b + y.b; } }
    k + x.v
}
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let k = g(p);
    println!("k={} n={}", k, rd(p));
    std::process::exit(if k != 7 { 2 } else if rd(p) != 21 { 1 } else { 0 });
}
