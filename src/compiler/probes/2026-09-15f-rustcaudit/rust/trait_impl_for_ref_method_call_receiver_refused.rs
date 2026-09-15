trait Get { fn get(self: Self) -> i64; }
impl<'a> Get for &'a i64 {
    fn get(self: Self) -> i64 {
        return *self;
    }
}
fn logos_main() -> i32 {
    let v: i64 = 19i64;
    let r = &v;
    return r.get() as i32;
}

fn main() { std::process::exit(logos_main()); }
