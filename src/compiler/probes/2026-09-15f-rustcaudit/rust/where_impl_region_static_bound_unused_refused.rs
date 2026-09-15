static V: i64 = 9i64;
struct W<'a> { r: &'a i64 }
impl<'a> W<'a> {
    fn keep(self: &Self) -> &'static i64 where 'a: 'static { return self.r; }
}
fn logos_main() -> i32 {
    let w = W { r: &V };
    let s = w.keep();
    return *s as i32 - 9i32;
}

fn main() { std::process::exit(logos_main()); }
