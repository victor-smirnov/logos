#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S<'a> { r: &'a i64 }
impl<'a> S<'a> {
    fn cb(&self, g: fn(&'a i64) -> i64) -> i32 {
        let f: fn(&i64) -> i64 = g;
        return 0i32;
    }
}
fn main() {}
