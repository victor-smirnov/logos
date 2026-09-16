// TWIN of bc_0915a_refeq_hb_e15_admit.logos ; TWIN: Eq -> PartialEq
struct S { v: i64, c: *mut i64 }
impl Drop for S { fn drop(&mut self) { unsafe { *self.c = *self.c + 1; } } }
impl PartialEq for S { fn eq(&self, other: &S) -> bool { self.v == other.v } }
fn run() -> i32 {
    let mut n: i64 = 0;
    let p: *mut i64 = &mut n;
    let mut hit: i64 = 0;
    {
        let a = S { v: 3, c: p }; let b = S { v: 3, c: p };
        let ra: &S = &a; let rb: &S = &b;
        if ra == rb { hit = 1; }
        if ra != rb { hit = 5; }
    }
    let got: i64 = unsafe { n };
    if got != 2 { return 10 + got as i32; }
    if hit != 1 { return 1; }
    0
}
fn main() { std::process::exit(run()); }
