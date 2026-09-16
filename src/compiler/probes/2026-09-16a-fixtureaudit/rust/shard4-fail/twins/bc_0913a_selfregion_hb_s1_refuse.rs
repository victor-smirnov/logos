#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct MyStruct<'a> { field: &'a i64 }
impl<'q> MyStruct<'q> {
    fn make<'a>(field: &'a i64) -> MyStruct<'a> {
        let s: Self = MyStruct { field: field };
        return s;
    }
}
fn main() {}
