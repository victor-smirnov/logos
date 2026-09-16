// verbatim twin of tests/imported/fail/regions/regions-bounded-by-trait-requiring-static--requiring-static.logos AS PORTED
fn assert_send<T: 'static>() { }
fn static_lifetime_ok<'a>(_x: &'a i64) { assert_send::<&'static i64>(); }
fn param_not_ok<'a>(_x: &'a i64) { assert_send::<&'a i64>(); }
fn main() { std::process::exit(0); }
