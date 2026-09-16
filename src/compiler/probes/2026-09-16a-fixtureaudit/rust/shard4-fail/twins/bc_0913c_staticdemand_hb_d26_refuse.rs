#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe, static_mut_refs)]
fn static_id_indirect<'a, 'b>(t: &'a i64) -> &'static i64 where 'a: 'b, 'b: 'static { return t; }
fn error(v: &i64) { let r = static_id_indirect(v); }
fn main() { let n = 1i64; error(&n); }
