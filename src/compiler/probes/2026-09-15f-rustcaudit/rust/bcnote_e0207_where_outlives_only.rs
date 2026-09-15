struct S { n: i64 }
trait Tr { fn go(&self) -> i64; }
impl<U> Tr for S where U: 'static {
    fn go(&self) -> i64 { return 5i64; }
}
fn main() {}
