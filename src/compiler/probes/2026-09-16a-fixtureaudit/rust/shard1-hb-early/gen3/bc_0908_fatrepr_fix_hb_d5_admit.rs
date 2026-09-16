// TWIN of tests/logos/pass/bc_0908_fatrepr_fix_hb_d5_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.lang.rc;` -> `use std::rc::Rc;`
// TWIN: rc_new(..) -> Rc::new(..)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
use std::rc::Rc;
// hand battery: round 2026-09-08-fatrepr-fix, program D5 — caught: verdict moved base -> landed — RESULT.md "CLOSED (REFUSED -> rc 0, RUN): D1 inherent method · D5 TRAIT method · D8 ... · D2 closure -> fn ptr"
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-08-fatrepr-fix/hand/D5.logos
// D5 — CLASS PROBE: the wrapper unsize at a TRAIT method (not inherent), so
// "inherent" is separated from "selector" once more, on today's binary.
trait Sp { fn v(&self) -> i64; }
struct A { x: i64 }
impl Sp for A { fn v(&self) -> i64 { return self.x; } }
trait Eater { fn eat(&self, r: Rc<dyn Sp>) -> i64; }
struct H { n: i64 }
impl Eater for H { fn eat(&self, r: Rc<dyn Sp>) -> i64 { return r.v() + self.n; } }
fn __logos_main() -> i32 {
    let h: H = H { n: 1i64 };
    let rc: Rc<A> = Rc::new(A { x: 17i64 });
    if h.eat(rc) != 18i64 { return 10i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

