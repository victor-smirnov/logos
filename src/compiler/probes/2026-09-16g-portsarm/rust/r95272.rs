// repaired port twin: invariant carrier restored (upstream uses Cell<&'a ()>)
use std::cell::Cell;
fn check<'a, 'b>(_x: Cell<&'a i64>, _y: Cell<&'b i64>) -> i64 where 'a: 'b { 1i64 }
fn test<'a, 'b>(x: Cell<&'a i64>, y: Cell<&'b i64>) -> i64 { check(x, y) }
fn main() { std::process::exit(0); }
