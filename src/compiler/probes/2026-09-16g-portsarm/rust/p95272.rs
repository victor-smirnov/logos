// verbatim twin of tests/imported/fail/nll/issue-95272.logos AS PORTED
fn check<'a, 'b>(_x: &'a i64, _y: &'b i64) -> i64 where 'a: 'b { 1i64 }
fn test<'a, 'b>(x: &'a i64, y: &'b i64) -> i64 { check(x, y) }
fn main() { std::process::exit(0); }
