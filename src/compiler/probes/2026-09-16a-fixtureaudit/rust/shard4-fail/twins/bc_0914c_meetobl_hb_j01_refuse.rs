#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum M<'a> { N, V(&'a mut &'a i64, &'a i64) }
fn f<'a, 'b>(m: &'a mut &'a i64, r: &'b i64) -> M<'a> {
    return M::V(m, r);
}
fn main() {}
