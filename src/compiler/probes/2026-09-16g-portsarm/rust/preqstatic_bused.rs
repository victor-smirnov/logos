// verbatim twin of tests/imported/fail/regions/regions-bounded-by-trait-requiring-static--b-used.logos AS PORTED
fn assert_send<T: 'static>(x: T) -> T { x }
fn param_not_ok<'a>(x: &'a i64) -> &'a i64 { assert_send::<&'a i64>(x) }
fn main() { std::process::exit(0); }
