struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
struct W { arr: [D; 2] }
fn mkw(p: *mut i64) -> W { W { arr: [D { v: 1, c: p }, D { v: 10, c: p }] } }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let k: i64;
    {
        k = mkw(p).arr[1].v;
    }
    let got: i64 = unsafe { n };
    println!("k={} n={}", k, got);
}
