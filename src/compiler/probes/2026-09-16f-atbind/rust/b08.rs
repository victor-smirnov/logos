struct D { v: i64, c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c * 10 + self.v; } } }
fn rd(p: *mut i64) -> i64 { unsafe { *p } }
fn g(p: *mut i64) -> i64 {
    let arr: [D; 2] = [D { v: 2, c: p }, D { v: 3, c: p }];
    match arr { y @ [_, _] => { return 9; } }
}
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let k = g(p);
    println!("k={} n={}", k, rd(p));
    std::process::exit(if k != 9 { 2 } else if rd(p) != 23 { 1 } else { 0 });
}
