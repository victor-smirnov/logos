struct S { n: i64 }
trait Tr { type Item; fn go(self: Self) -> i64; }
impl<U> Tr for S {
    type Item = U;
    fn go(self: Self) -> i64 { return 5i64; }
}
fn main() {}
