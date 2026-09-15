struct Foo<'s> { r: &'s i64 }
impl Foo<'_> {
    fn remake(self: &Self) -> i64 {
        let mk = |q: &i64| -> i64 { return *q; };
        return mk(self.r);
    }
}
fn logos_main() -> i32 {
    let v: i64 = 69i64;
    let f = Foo { r: &v };
    return f.remake() as i32;
}

fn main() { std::process::exit(logos_main()); }
