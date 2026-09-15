fn main() {
    let n: i64 = 6i64;
    if **n != 6i64 { std::process::exit(1); }
    std::process::exit(0);
}
