// TWIN of tests/logos/pass/bc_0914e_thruref_hb_t1_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.boxed;` dropped (prelude in Rust)
// TWIN: box_new(..) -> Box::new(..)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program t1 — caught: verdict moved base -> landed — PROBES.md "asgref (7 moved, all LEGAL, all RUN): m1 m2 m5 t1 t2 t3"; landed 2026-09-14f, measured base REFUSED -> today RUN
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/walk.ek13/t1_explicit_ref_list_walk.logos
enum List { Cons(i64, Box<List>), Nil }
fn __logos_main() -> i32 {
    let lst: List = List::Cons(1i64, Box::new(List::Cons(2i64, Box::new(List::Nil))));
    let mut sum: i64 = 0i64;
    let mut cur: &List = &lst;
    loop {
        match *cur {
            List::Cons(ref v, ref rest) => { sum = sum + *v; cur = &**rest; }
            List::Nil => { break; }
        }
    }
    return (sum - 3i64) as i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

