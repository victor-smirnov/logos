trait Get<'a, 'b> {
    fn get(self: &Self) -> &'b i64;
}
struct H<'a> {
    r: &'a i64,
}
impl<'a: 'b, 'b> Get<'a, 'b> for H<'a> {
    fn get(self: &Self) -> &'a i64 {
        return self.r;
    }
}
fn logos_main() -> i32 {
    let v = 7i64;
    let h = H { r: &v };
    if *h.get() != 7i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
