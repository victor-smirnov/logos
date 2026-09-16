struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn rd(p: *mut i64) -> i64 { unsafe { *p } }
struct Q { u: i64, w: i64 }
fn g() -> i64 {
    let q = Q { u: 6, w: 8 };
    match q { y @ Q { .. } => { return y.u * 10 + y.w; } }
}
fn main() { let k = g(); println!("k={}", k); std::process::exit(if k != 68 { 1 } else { 0 }); }
