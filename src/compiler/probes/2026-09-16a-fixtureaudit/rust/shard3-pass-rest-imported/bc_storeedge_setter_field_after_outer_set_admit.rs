struct H<'a> { r: &'a i64 }
impl<'a> H<'a> {
    fn set(&mut self, r: &'a i64) { self.r = r; }
}
fn main() {
    let x: i64 = 5i64;
    let y: i64 = 6i64;
    let mut h: H = H { r: &y };
    h.set(&x);
    {
        let d: i64 = 1i64;
        h.set(&d);
    }
    let v: i64 = x;
    std::process::exit((v as i32) - 5i32);
}
