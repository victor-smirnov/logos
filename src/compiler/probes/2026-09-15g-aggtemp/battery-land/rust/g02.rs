struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let mut s: i64 = 0;
    while [D { v: 1, c: p }; 1][0].v > 100 {
        s += 1;
    }
    let got: i64 = unsafe { n };
    println!("s={} n={}", s, got);
}
