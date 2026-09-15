trait Get<'a> {
    fn get(self: &Self) -> &'a i64;
}
struct H<'q> {
    r: &'q i64,
}
impl<'z> Get<'z> for H<'z> {
    fn get(self: &Self) -> &'z i64 {
        return self.r;
    }
}
fn logos_main() -> i32 {
    let v = 5i64;
    let h = H { r: &v };
    if *h.get() != 5i64 { return 1i32; }
    return 0i32;
}

fn main() { std::process::exit(logos_main()); }
