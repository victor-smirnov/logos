// TWIN of tests/logos/pass/bc_0914o_autoreffund_hb_n04_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.string;` dropped (prelude in Rust)
// TWIN: extern fn printf dropped; calls translated to Rust print!
// TWIN: printf(..) -> Rust print!(..): %ld/%s -> {} (a Rust str is not NUL-terminated, so a C printf over-reads)
// TWIN: self: &mut T -> &mut self
// TWIN: .len() -> .len() as i64 (Rust len is usize, Logos i64)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14o-autoreffund, program n04 — caught: legal, correct on base and under the landing; REFUSED with E0507 under the priced neighbour arm fieldinplace (a field base borrowed in place), which it condemns
// legality: by reading, no rustc binary
struct C { v: i64, c: *mut i64 }
impl Drop for C { fn drop(&mut self) { unsafe { *self.c = *self.c + self.v; } } }
struct P { name: String, tag: C }
fn mkp(c: *mut i64) -> P {
    return P { name: String::from("abcdefghijklmnopqrstuvwxyz"), tag: C { v: 1000i64, c: c } };
}
fn __logos_main() -> i32 {
    let mut n: i64 = 0i64;
    let p: *mut i64 = &mut n;
    let mut len: i64 = 0i64;
    {
        let s: String = mkp(p).name;
        len = s.len() as i64 as i64;
    }
    let got: i64 = unsafe { n };
    unsafe { print!("len={} n={}\n", len, got); }
    if len != 26i64 { return 2i32; }
    if got != 1000i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

