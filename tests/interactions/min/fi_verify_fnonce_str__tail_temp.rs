struct D { c: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.c = *self.c + 1; } } }
impl D { fn get(&self) -> i64 { 5 } }
fn g(p: *mut i64) -> i64 { D { c: p }.get() }
fn main() {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let r = g(p);
    println!("{} {}", r, unsafe { n });
}
