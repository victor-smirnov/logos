// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_c20_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.lang.cmp;` dropped (prelude in Rust)
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: impl Eq{fn eq} -> impl PartialEq{fn eq} + marker impl Eq (Rust splits the trait; Logos Eq carries eq)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14o-autoreffund, program c20 — caught: `>` via partial_cmp with a side-effecting place left operand: base leaked the right temporary (12); source order and drop under the landing (126)
// legality: by reading, no rustc binary
struct D { v: i64, s: *mut i64 }
impl Drop for D { fn drop(&mut self) { unsafe { *self.s = *self.s * 10i64 + self.v + 4i64; } } }
impl PartialEq for D { fn eq(&self, other: &D) -> bool { return self.v > 0i64 && other.v > 0i64; } }
fn mk(v: i64, s: *mut i64) -> D {
    unsafe { *s = *s * 10i64 + v; }
    return D { v: v, s: s };
}
fn side(s: *mut i64) -> i64 {
    unsafe { *s = *s * 10i64 + 1i64; }
    return 1i64;
}
fn pick<'a>(d: &'a D, s: *mut i64) -> &'a D {
    unsafe { *s = *s * 10i64 + 1i64; }
    return d;
}
fn rd(s: *mut i64) -> i64 { return unsafe { *s }; }

impl PartialOrd for D {
    fn partial_cmp(&self, other: &D) -> Option<Ordering> {
        if self.v < other.v { return Some(Ordering::Less); }
        if self.v > other.v { return Some(Ordering::Greater); }
        return Some(Ordering::Equal);
    }
}
fn __logos_main() -> i32 {
    let mut q: i64 = 0i64;
    let s: *mut i64 = &mut q;
    let mut got: i64 = 0i64;
    {
        let a: D = D { v: 3i64, s: s };
        if !(*pick(&a, s) > mk(2i64, s)) { return 3i32; }
        got = rd(s);
    }
    unsafe { print!("seq={}\n", got); }
    if got != 126i64 { return 1i32; }
    return 0i32;
}
impl Eq for D {}

fn main() { std::process::exit(__logos_main() as i32); }

