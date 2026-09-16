struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn rd(p: *mut i64) -> i64 { unsafe { *p } }
struct P<T> { x: T, d: D }
fn g(p: *mut i64) -> i64 {
    let s: P<i64> = P::<i64> { x: 4, d: D { v: 2, c: p } };
    match s { y @ P { .. } => { return y.x; } }
}
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let k = g(p);
    println!("k={} n={}", k, rd(p));
    std::process::exit(if k != 4 { 2 } else if rd(p) != 2 { 1 } else { 0 });
}
