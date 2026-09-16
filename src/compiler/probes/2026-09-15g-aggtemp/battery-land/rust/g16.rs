struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c += self.v; } } }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let k: i64;
    {
        let a = D { v: 1, c: p };
        let b = D { v: 10, c: p };
        let arr: [D; 2] = [a, b];
        k = arr[1].v;
    }
    let got: i64 = unsafe { n };
    println!("k={} n={}", k, got);
}
