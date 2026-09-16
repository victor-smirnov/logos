#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn compare_fn_ptr<'a, 'b, 'c>(f: fn(&'c mut &'a i64), g: fn(&'c mut &'b i64)) -> bool { return f == g; }
fn main() {}
