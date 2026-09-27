

struct S<T> { x: T }
impl<T: PartialEq> S<T> {
    fn f(&self) -> i64 { 7 }
}
fn main() {
    let c: S<i64> = S { x: 1 };
    println!("[{}]", c.f());
}
