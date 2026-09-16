struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let inside: i64;
    {
        let r: &mut D = &mut D { v: 1, c: p };
        r.v = 7;
        inside = unsafe { *p };
    }
    let got: i64 = unsafe { n };
    println!("inside={} n={}", inside, got);
}
