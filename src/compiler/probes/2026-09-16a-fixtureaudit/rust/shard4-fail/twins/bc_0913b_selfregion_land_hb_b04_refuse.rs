#![allow(dead_code, unused_variables, unused_mut, unused_assignments, unused_unsafe)]
struct P<'a, 'b> { x: &'a i64, y: &'b i64 }
impl P<'_, '_> {
    fn swap(&self) -> Self {
        return Self { x: self.y, y: self.x };
    }
}
fn main() {}
