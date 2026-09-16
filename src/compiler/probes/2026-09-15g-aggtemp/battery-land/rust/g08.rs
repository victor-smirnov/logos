struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
fn g(p: *mut i64) -> i64 {
    if [D { v: 1, c: p }, D { v: 10, c: p }][1].v == 10 {
        return 5;
    }
    return 0;
}
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let r: i64 = g(p);
    let got: i64 = unsafe { n };
    println!("r={} n={}", r, got);
}
