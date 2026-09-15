struct M<'a> { r: &'a mut i64 }
impl<'a> M<'a> {
    fn set(self: &mut M<'a>, r: &'a mut i64) { self.r = r; }
}
fn logos_main() -> i32 {
    let mut g: i64 = 1i64;
    let mut x: i64 = 5i64;
    let mut m: M = M { r: &mut g };
    m.set(&mut x);
    {
        let mut d: i64 = 2i64;
        m.set(&mut d);
    }
    let y: i64 = x;
    return (y as i32) - 5i32;
}

fn main() { std::process::exit(logos_main()); }
