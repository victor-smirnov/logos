// verbatim twin of tests/imported/fail/regions/regions-bounded-method-type-parameters-trait-bound.logos AS PORTED
fn method<'x, 'y: 'x>(_x: &'x i64, _y: &'y i64) { }
fn caller2<'a, 'b>(a: &'a i64, b: &'b i64) { method(a, b); }
fn main() { std::process::exit(0); }
