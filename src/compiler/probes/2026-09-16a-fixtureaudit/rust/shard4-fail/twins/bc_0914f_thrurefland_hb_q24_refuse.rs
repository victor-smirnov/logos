#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
fn f<'a, 'b>(x: fn(&'a mut &'a i64), y: fn(&'b mut &'b i64)) -> bool {
    return x >= y;
}
fn main() {}
