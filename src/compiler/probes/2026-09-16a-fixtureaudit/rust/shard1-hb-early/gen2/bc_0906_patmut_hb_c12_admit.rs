// TWIN of tests/logos/pass/bc_0906_patmut_hb_c12_admit.logos
// envelope translated, BODY VERBATIM
// TWIN: package decl dropped
// TWIN: `use logos.mem.collections.vec;` dropped (prelude in Rust)
// TWIN: Option::Some/None -> prelude
// TWIN: fn main()->i32 illegal in Rust; wrapped, exit code preserved
// hand battery: round 2026-09-06-patmut, program c12 — caught: predicted wrong — predictions-2026-09-06b.txt:9 "c05 c06 c07 c12 ... refused -> run 0"; PROBES.md "c05 c06 c07 ... RAN on base too"; c12 "refused" (PROBES.md)
// legality: by reading, no rustc binary
// harvested 2026-09-14h from src/compiler/probes/2026-09-06-patmut/hand2/c12_whilelet_mut_box_deref_write.logos
fn __logos_main() -> i32 {
    let mut v: Vec<Box<i64>> = Vec::new();
    v.push(Box::new(1i64));
    v.push(Box::new(2i64));
    let mut acc: i64 = 0i64;
    while let Some(mut bb) = v.pop() {
        *bb = *bb + 10i64;
        acc = acc + *bb;
    }
    if acc != 23i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(__logos_main() as i32); }

