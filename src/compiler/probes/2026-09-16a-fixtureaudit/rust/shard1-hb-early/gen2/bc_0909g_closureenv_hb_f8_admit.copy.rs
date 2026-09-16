// TWIN of tests/logos/pass/bc_0909g_closureenv_hb_f8_admit.logos  [A16 --copy variant]
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-09g-closureenv, program F8 — caught: refuted an arm — closureenv/RESULT.md "`capfat` REFUSES two legal programs (F8, R6)"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-09g-closureenv/hand/F8.logos
// A CLOSURE captured by a closure — Kind::Closure, the fourth fat kind, and
// the one the struct-field arm also inlines. Boxed so the env is heap.
fn f(inner: ||->i64, other: ||->i64) -> Box<dyn Fn() -> i64> {
    return Box::new(move || { return inner() + other(); });
}
fn __logos_main() -> i32 {
    let a = || -> i64 { return 4i64; };
    let b = || -> i64 { return 5i64; };
    let c: Box<dyn Fn() -> i64> = f(a, b);
    if c() != 9i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

