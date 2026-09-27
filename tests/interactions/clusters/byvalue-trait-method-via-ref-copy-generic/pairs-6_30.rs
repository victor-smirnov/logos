trait Widen { fn widen(self) -> i64; }
impl Widen for u8 { fn widen(self) -> i64 { self as i64 } }
fn one<T: Widen + Copy>(x: &T) -> i64 { x.widen() }
fn main() {
    let b = 7u8;
    std::process::exit(one(&b) as i32);
}
