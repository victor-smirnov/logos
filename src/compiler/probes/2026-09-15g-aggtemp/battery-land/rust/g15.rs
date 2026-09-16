struct H { m: Box<[u8; 16]>, c: *mut i64, v: i64 }
impl Drop for H { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let k: i64;
    {
        k = [H { m: Box::new([0u8; 16]), c: p, v: 1 },
             H { m: Box::new([0u8; 16]), c: p, v: 10 }][1].v;
    }
    let got: i64 = unsafe { n };
    println!("k={} n={}", k, got);
}
