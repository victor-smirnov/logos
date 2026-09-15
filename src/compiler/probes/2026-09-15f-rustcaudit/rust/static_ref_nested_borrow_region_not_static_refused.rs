static G: i64 = 9i64;
static R: &i64 = &G;
fn take(x: & &'static i64) -> i64 {
    return **x;
}
fn logos_main() -> i32 {
    let r: & &'static i64 = &&G;
    let a: i64 = take(&R);
    return (**r + a - 18i64) as i32;
}

fn main() { std::process::exit(logos_main()); }
