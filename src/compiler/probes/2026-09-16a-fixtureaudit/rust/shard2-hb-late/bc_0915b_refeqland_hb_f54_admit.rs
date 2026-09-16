// TWIN: Eq -> PartialEq
struct S { v: i64, c: *mut i64 }
impl Drop for S { fn drop(&mut self) { unsafe { *self.c = *self.c + 1; } } }
impl PartialEq for S { fn eq(&self, other: &S) -> bool { self.v == other.v } }
struct P { x: S, n: i64 }
impl PartialEq for P { fn eq(&self, o: &P) -> bool { &self.x == &o.x && self.n == o.n } }
fn run() -> i32 {
    let mut cnt: i64 = 0;
    let c: *mut i64 = &mut cnt;
    let mut hit: i64 = 0;
    {
        let p = P { x: S { v: 1, c }, n: 2 };
        let q = P { x: S { v: 1, c }, n: 2 };
        let rp: &P = &p; let rq: &P = &q;
        if rp == rq { hit = hit + 1; }
        if p == q { hit = hit + 1; }
    }
    let got: i64 = unsafe { cnt };
    if got != 2 { return 10 + got as i32; }
    if hit != 2 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
