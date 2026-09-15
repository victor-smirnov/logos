fn main() {
    let mut n: i64 = 0i64;
    let mut c = |x: i64| { n = n + x; return n; };
    let r: i64 = c(c(1i64));
    if r != 2i64 { std::process::exit(1); }
    std::process::exit(0);
}
