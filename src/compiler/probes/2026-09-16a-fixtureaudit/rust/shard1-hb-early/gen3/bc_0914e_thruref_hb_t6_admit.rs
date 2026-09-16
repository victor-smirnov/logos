// TWIN of tests/logos/pass/bc_0914e_thruref_hb_t6_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.boxed;` dropped (prelude in Rust)
// TWIN: box_new(..) -> Box::new(..)
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-14e-thruref, program t6 — caught: refuted an arm — PROBES.md "pbdbm (15 moved): ... REFUSED LEGAL b10 k01 k05 k08 (a use of the scrutinee after the binding's LAST use) and t4 t5 t6 m14 m15 (an assig...
// legality: by reading, no rustc binary
// harvested 2026-09-14i from snapshot hand-harvest-2026-09-14b/walk.ek13/t6_struct_walk_mut_list.logos
enum List { Cons(i64, Box<List>), Nil }
fn __logos_main() -> i32 {
    let mut lst: List = List::Cons(1i64, Box::new(List::Cons(2i64, Box::new(List::Nil))));
    let mut cur: &mut List = &mut lst;
    loop {
        match cur {
            List::Cons(v, rest) => { *v = *v + 10i64; cur = &mut **rest; }
            List::Nil => { break; }
        }
    }
    match lst { List::Cons(v, _) => { return (v - 11i64) as i32; } List::Nil => { return 9i32; } }
}

fn main() { std::process::exit(__logos_main() as i32); }

