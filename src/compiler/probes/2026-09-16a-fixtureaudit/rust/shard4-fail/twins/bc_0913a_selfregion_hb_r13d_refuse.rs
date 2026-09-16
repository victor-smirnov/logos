#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct MyStruct<'a> { field: &'a i64 }
impl MyStruct<'_> {
    fn make<'a>(field: &'a i64) -> Self { return MyStruct { field: field }; }
}
fn main() {}
