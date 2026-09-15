fn twice_ten_om(f: &mut dyn FnMut(i64) -> i64) -> i64 {
    return f(f(10i64));
}
fn main() {
    let mut n: i64 = 0i64;
    let mut c = |x: i64| { n = n + x; return n; };
    let r: i64 = twice_ten_om(&mut c);
    std::process::exit(0);
}
