fn main() {
    fn f(x: i32) -> i32 { if x == 0 { 7 } else { f(x - 1) } }
    std::process::exit(f(3));
}
