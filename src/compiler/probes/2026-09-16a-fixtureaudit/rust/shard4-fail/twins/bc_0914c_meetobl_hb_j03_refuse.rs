#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
enum W<'a> { N, V(&'a i64, &'a mut &'a i64) }
fn f<'a, 'b>(r: &'b i64, m: &'a mut &'a i64) -> W<'a> {
    return W::V(r, m);
}
fn main() {}
