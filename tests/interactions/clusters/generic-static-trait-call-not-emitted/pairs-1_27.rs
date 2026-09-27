trait Peano { const N: i64; }
struct Z; struct S<P>(P);
impl Peano for Z { const N: i64 = 0; }
impl<P: Peano> Peano for S<P> { const N: i64 = P::N + 1; }
fn n<T: Peano>() -> i64 { return T::N; }
fn main() { let a = n::<S<S<Z>>>(); println!("{}", a); let _ = S(Z); }
