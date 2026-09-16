// repaired port twin, user-struct invariant carrier (no Cell needed)
struct Inv<'a> { p: &'a mut &'a i64 }
fn check<'a, 'b>(_x: Inv<'a>, _y: Inv<'b>) -> i64 where 'a: 'b { 1i64 }
fn test<'a, 'b>(x: Inv<'a>, y: Inv<'b>) -> i64 { check(x, y) }
fn main() { std::process::exit(0); }
