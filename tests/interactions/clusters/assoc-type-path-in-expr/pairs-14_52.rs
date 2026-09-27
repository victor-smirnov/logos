trait Dim { type Elem: Default + std::fmt::Debug; }
struct D2;
impl Dim for D2 { type Elem = u8; }
fn z<D: Dim>() -> D::Elem { D::Elem::default() }
fn main() { println!("{:?}", z::<D2>()); }
