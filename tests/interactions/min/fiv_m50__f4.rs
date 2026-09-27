trait T {
    fn a(self) -> Self where Self: Sized { self }
    fn b(self) -> Self where Self: Sized { self }
}
enum E { X, Y }
impl T for E {}
fn main() { let e = E::Y; let f = e.b(); std::process::exit(match f { E::Y => 7, _ => 1 }); }
