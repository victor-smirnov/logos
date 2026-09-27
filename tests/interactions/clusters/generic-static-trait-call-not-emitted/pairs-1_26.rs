trait Store { type Buf; fn sum(b: &Self::Buf) -> i64; }
struct TupStore;
impl Store for TupStore { type Buf = (i64, i64); fn sum(b: &(i64, i64)) -> i64 { return b.0 * 100 + b.1; } }
fn go<S: Store>(b: &S::Buf) -> i64 { return S::sum(b); }
fn main() { let b: (i64, i64) = (2, 5); println!("{}", go::<TupStore>(&b)); }
