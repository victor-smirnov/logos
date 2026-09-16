// TWIN: `impl Eq`/`T: Eq` translated to Rust's `PartialEq` (Logos `Eq` is the eq-providing trait).
struct D { v: i64 }
impl PartialEq for D {
    fn eq(&self, other: &D) -> bool {
        return self.v == other.v;
    }
}
fn same<T: PartialEq>(a: &T, b: &T) -> bool {
    return a == b;
}
fn main() {
    let a: D = D { v: 4i64 };
    let b: D = D { v: 4i64 };
    if !same::<D>(&a, &b) { std::process::exit(1); }
    let c: char = 'k';
    let d: char = 'k';
    if !same::<char>(&c, &d) { std::process::exit(2); }
    std::process::exit(0);
}
