// RUST TWIN of generic_unbounded_typevar_reuse_after_tuple_literal_admits.logos (mechanical, tools: l2rs.py of round 2026-09-15e-consume)
#![allow(unused, dead_code, unused_mut, unused_variables, unused_assignments, unreachable_code)]
use std::ops::*;
// SOUNDNESS QUEUE row generic_unbounded_typevar_reuse_after_tuple_literal_admits (tier 2). `fn f<T>(x: T) -> T { let a: (T, i64) = (x, 1i64);
// return x; }` COMPILES; rustc refuses (E0382, T carries no Copy bound). The decision is DELIBERATE and commented: TUPLE_LIT in
// SemaChecker::lower_expr_inner skips mark_moved_expr for a TypeVar element because "Logos is lenient about generic reuse (`(x, x)` for `x: T`
// may be Copy at mono — gen-tuple-* rely on this)". That leniency is in NO divergence registry (docs/DIVERGENCES.md A1-A17 and docs/spec searched
// by construct, 2026-09-15e) — so by the standing rule the answer is Rust's, and the fixtures that rely on it are a corpus decision with an owner.
// NEIGHBOUR of 2026-09-15e-consume's arrmovetv, ROWED WITH REASON 3 (its own cost is unpriced): the tuple arm was not built; its comment names
// the fixtures it would refuse. Found by hand program f12 (the tuple twin of f11). Legality by reading; no rustc binary.
fn f<T>(x: T) -> T {
    let a: (T, i64) = (x, 1i64);
    return x;
}
fn logos_main() -> i32 {
    let q: i64 = f(7i64);
    if q != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()) }
