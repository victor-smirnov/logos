struct M<'a> { r: &'a mut i64 }
impl<'a> M<'a> {
    fn set(self: &mut M<'a>, r: &'a mut i64) { self.r = r; }
}
fn main() {
    let mut g: i64 = 1i64;
    let mut x: i64 = 5i64;
    let mut m: M = M { r: &mut g };
    m.set(&mut x);
    {
        let mut d: i64 = 2i64;
        m.set(&mut d);
    }
    std::process::exit(*m.r as i32);
}
