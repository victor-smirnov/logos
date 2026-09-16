#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a, 'b>(x: *const (i64, &'a mut &'a i64), y: *const (i64, &'b mut &'b i64)) -> bool {
    return x == y;
}
fn main() {}
