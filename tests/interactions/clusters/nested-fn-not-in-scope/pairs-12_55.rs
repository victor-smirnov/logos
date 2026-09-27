fn main() {
    fn fact(n: i64) -> i64 { if n == 0 { return 1; } return n * fact(n - 1); }
    fn plain(n: i64) -> i64 { return n + 1; }
    println!("{} {}", plain(1), fact(5));
}
