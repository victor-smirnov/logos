// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_x02_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: extern fn printf -> unsafe extern "C" block
// TWIN: self: &mut T -> &mut self
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
unsafe extern "C" { fn printf(fmt: *const u8, ...) -> i32; }
// hand battery: round 2026-09-14o-autoreffund, program x02 — caught: `let r: &&C = &&C { .. }` never dropped the extended temporary on base (n=0); dropped at the end of the block under the landing (1000)
// legality: by reading, no rustc binary
struct C { v: i64, c: *mut i64 }
impl Drop for C { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
fn __logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let mut inside: i64 = 0i64;
    {
        let r: &&C = &&C { v: 1000i64, c: p };
        if r.v != 1000i64 { return 3i32; }
        inside = unsafe { n };
    }
    let got: i64 = unsafe { n };
    unsafe { printf("inside=%ld n=%ld\n".as_ptr(), inside, got); }
    if inside != 0i64 { return 2i32; }
    if got != 1000i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

