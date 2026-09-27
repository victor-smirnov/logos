#![allow(dead_code, unused)]
enum L<T> { C(Box<L<T>>), N(T) }
impl<T> L<T> {
    fn a(&self) -> i64 { match self { L::C(t) => t.a(), L::N(_) => 0 } }
}
fn g(c: &L<u8>) {}
fn main() { let l: L<i64> = L::N(1); let r = l.a(); println!("{}", r); }
