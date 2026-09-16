struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn g() -> i64 {
    let t: (i64, i64) = (11, 22);
    match t { y @ (_, _) => { return y.0 * 100 + y.1; } }
}
fn main() {
    let k = g();
    println!("k={}", k);
    std::process::exit(if k != 1122 { 1 } else { 0 });
}
