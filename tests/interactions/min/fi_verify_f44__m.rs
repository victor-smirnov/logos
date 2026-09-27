fn main() {
    let a: Vec<i64> = vec![1, 2];
    let b: Vec<i64> = vec![1, 2];
    if a == b { std::process::exit(0); }
    std::process::exit(1);
}
