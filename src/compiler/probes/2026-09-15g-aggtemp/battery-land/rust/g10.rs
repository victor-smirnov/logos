struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let mut s: i64 = 0;
    {
        let a = D { v: 1, c: p };
        let b = D { v: 10, c: p };
        for d in [a, b] {
            for e in [D { v: 100, c: p }] {
                s += e.v;
            }
            s += d.v;
        }
    }
    let got: i64 = unsafe { n };
    println!("s={} n={}", s, got);
}
