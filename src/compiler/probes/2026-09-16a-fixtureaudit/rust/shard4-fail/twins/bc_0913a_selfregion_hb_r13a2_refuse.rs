#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct MyStruct<'a> { field: &'a i64 }
trait Trait<'a> { fn tmake(field: &'a i64) -> MyStruct<'a>; }
impl<'a> Trait<'a> for MyStruct<'_> {
    fn tmake(field: &'a i64) -> MyStruct<'a> { return Self { field: field }; }
}
fn main() {}
