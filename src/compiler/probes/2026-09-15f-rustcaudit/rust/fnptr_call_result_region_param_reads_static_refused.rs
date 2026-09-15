fn f<'r>(g: fn(&'r i64) -> i64) -> fn(&'r i64) -> i64 {
    return g;
}
fn rd(x: &i64) -> i64 { return *x; }
fn logos_main() -> i32 {
    let h = f(rd);
    let v: i64 = 18i64;
    return h(&v) as i32;
}

fn main() { std::process::exit(logos_main()); }
