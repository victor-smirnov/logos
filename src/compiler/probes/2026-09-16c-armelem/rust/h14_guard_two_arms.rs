struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn g(p: *mut i64, sel: i64) -> i64 {
    let arr: [D; 2] = [D { v: 1, c: p }, D { v: 2, c: p }];
    match arr { [a, _] if sel == 0 => { return a.v; } [_, b] => { return b.v; } }
}
fn main() { let mut n: i64 = 0; let p: *mut i64 = &mut n; let k = g(p, 1); println!("k={} n={}", k, unsafe { n }); }
