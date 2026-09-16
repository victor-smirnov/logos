// TWIN: Eq -> PartialEq
struct D { v: i64 }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { self.v == other.v } }
fn tick<'a>(s: *mut i64, k: i64, d: &'a D) -> &'a D {
    unsafe { *s = *s * 10 + k; }
    d
}
fn run() -> i32 {
    let a = D { v: 4 }; let b = D { v: 4 };
    let mut seq: i64 = 0;
    let sp: *mut i64 = &mut seq;
    let same: bool = tick(sp, 1, &a) == tick(sp, 2, &b);
    if !same { return 1; }
    let got: i64 = unsafe { *sp };
    if got != 12 { return 2; }
    0
}
fn main() { std::process::exit(run()); }
