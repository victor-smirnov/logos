fn main() {
    let a: (i64, i64) = (1i64, 9i64);
    let b: (i64, i64) = (1i64, 9i64);
    let ra: &(i64, i64) = &a;
    let rb: &(i64, i64) = &b;
    if ra < rb { std::process::exit(1); }
    if !(ra <= rb) { std::process::exit(2); }
    std::process::exit(0);
}
