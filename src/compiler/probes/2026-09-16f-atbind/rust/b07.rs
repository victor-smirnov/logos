struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn rd(p: *mut i64) -> i64 { unsafe { *p } }
enum E { A(i64), B(i64) }
fn g() -> i64 {
    let e = E::B(7);
    match e { y @ (E::A(_) | E::B(_)) => { return 5; } }
}
fn main() { let k = g(); println!("k={}", k); std::process::exit(if k != 5 { 1 } else { 0 }); }
