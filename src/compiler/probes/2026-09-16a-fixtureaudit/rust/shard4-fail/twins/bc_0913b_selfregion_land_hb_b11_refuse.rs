#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct Foo<'s> { r: &'s i64 }
impl Foo<'_> {
    fn stash(&self, x: &i64) -> i64 {
        let g: Self = Foo { r: x };
        return *g.r;
    }
}
fn main() {}
