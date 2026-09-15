struct Run<F> {
    f: F,
}
impl<F> Run<F> {
    fn go(self: &Self) -> i64
    where F: Fn(i64) -> i64
    {
        return (self.f)(2i64);
    }
}
fn logos_main() -> i32 {
    let r = Run { f: |x: i64| x + 7i64 };
    if r.go() != 9i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
