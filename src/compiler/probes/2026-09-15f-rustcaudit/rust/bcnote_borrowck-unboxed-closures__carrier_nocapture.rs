fn main() {
    let mut c = |x: i64| { return x + 1i64; };
    let r = c(c(10i64));
    std::process::exit(r as i32);
}
