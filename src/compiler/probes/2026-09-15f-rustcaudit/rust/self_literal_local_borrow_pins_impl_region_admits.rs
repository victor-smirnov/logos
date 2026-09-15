struct Foo<'s> { r: &'s i64 }
impl<'s> Foo<'s> {
    fn peek(self: &Self) -> i64 {
        let v: i64 = 40i64;
        let t = Self { r: &v };
        return *t.r;
    }
}
fn main() {
    let w: i64 = 1i64;
    let f = Foo { r: &w };
    std::process::exit(f.peek() as i32);
}
