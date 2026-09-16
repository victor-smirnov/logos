// TWIN of tests/logos/pass/bc_0908_fatrepr_fix_hb_d8_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.lang.rc;` -> `use std::rc::Rc;`
// TWIN: rc_new(..) -> Rc::new(..)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
use std::rc::Rc;
// hand battery: round 2026-09-08-fatrepr-fix, program D8 — caught: verdict moved base -> landed — RESULT.md "CLOSED (REFUSED -> rc 0, RUN): D1 inherent method · D5 TRAIT method · D8 ... · D2 closure -> fn ptr"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr-fix/hand/D8.logos
// D8 — the wrapper unsize at an OVERLOADED method name: two candidates, one
// `Rc<dyn Sp>` and one `i64`. A selector that admits too much picks wrong.
trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { return self.x; } }
struct H { n: i64 }
impl H {
    pub fn eat(&self, k: i64) -> i64 { return k * 2i64; }
    pub fn eat(&self, r: Rc<dyn Sp>) -> i64 { return r.v() + self.n; }
}
fn __logos_main() -> i32 {
    let h: H = H { n: 1i64 };
    let rc: Rc<A> = Rc::new(A { x: 17i64 });
    if h.eat(rc) != 18i64 { return 10i32; }
    if h.eat(5i64) != 10i64 { return 11i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

