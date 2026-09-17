fn less<T: Ord>(a: &T, b: &T) -> bool { a < b }
fn main() {
    let x: u64 = 18446744073709551615; let y: u64 = 1;
    if less::<u64>(&x, &y) { std::process::exit(1); }
}
