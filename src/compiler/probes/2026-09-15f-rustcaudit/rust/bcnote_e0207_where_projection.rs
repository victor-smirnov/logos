struct W<T> { t: T }
trait Tr { fn go(&self) -> i64; }
impl<T, U> Tr for W<T> where T: Iterator<Item = U> {
    fn go(&self) -> i64 { return 5i64; }
}
fn main() {}
