fn go<'a, 'b>(x: &'a i64, y: &'b i64) -> &'a i64 {
    let f = || -> &i64 { return y; };
    return f();
}
fn main() {
    let a: i64 = 2i64;
    let b: i64 = 5i64;
    std::process::exit(*go(&a, &b) as i32);
}
