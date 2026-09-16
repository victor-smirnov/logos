#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct S<'a> { p: *mut &'a i64 }
impl<'a> S<'a> {
    fn same<'b>(&self, o: *mut &'b i64) -> bool {
        return self.p == o;
    }
}
fn main() {}
