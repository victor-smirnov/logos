// TWIN: Eq -> PartialEq
struct S { v: i64, c: *mut i64 }
impl Drop for S { fn drop(&mut self) { unsafe { *self.c = *self.c + 1; } } }
impl PartialEq for S { fn eq(&self, other: &S) -> bool { self.v == other.v } }
fn mk(v: i64, c: *mut i64) -> S { S { v, c } }
fn run() -> i32 {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let eq: bool = &mk(3, p) == &mk(3, p);
    let got: i64 = unsafe { n };
    if got != 2 { return 10 + got as i32; }
    if !eq { return 1; }
    0
}
fn main() { std::process::exit(run()); }
